#pragma once

#include "Backend/GpuBuffer.hpp"
#include "VertexList.hpp"

#include <Math/Quat.hpp>

namespace fx {

struct MeshBone
{
    Vec3f Position;
    Quat Rotation;
};

class PrimitiveMesh
{
public:
    PrimitiveMesh() = default;

    template <renderer::eVertexType TVertexType>
    PrimitiveMesh(SizedArray<renderer::Vertex<TVertexType>>& vertices)
    {
        UploadVertices(vertices);
    }

    template <renderer::eVertexType TVertexType>
    PrimitiveMesh(SizedArray<renderer::Vertex<TVertexType>>& vertices, SizedArray<uint32>& indices)
    {
        CreateFromData(vertices, indices);
    }

    /**
     * @brief Recalculates normals if needed and uploads the vertex list to the GPU.
     */
    inline void UploadVertices()
    {
        if (VertexList.SupportsNormals() && !VertexList.HasNormals()) {
            LogDebug("Calculating normals for mesh");
            RecalculateNormals();
        }

        VertexList.UploadToGpu();
    }

    /**
     * @brief Uploads the vertices to the mesh's GPU buffers and copies the data into a cpu-side buffer if the property
     * `KeepInMemory` is true.
     */
    template <renderer::eVertexType TVertexType>
    void UploadVertices(const SizedArray<renderer::Vertex<TVertexType>>& vertices)
    {
        VertexList.CreateAsCopyOf<TVertexType>(vertices);
        UploadVertices();
    }

    template <renderer::eVertexType TVertexType>
    void UploadVertices(SizedArray<renderer::Vertex<TVertexType>>&& vertices)
    {
        VertexList.CreateFrom<TVertexType>(std::move(vertices));
        UploadVertices();
    }

    /** @brief Uploads mesh indices to a primitive mesh. */
    void UploadIndices(const SizedArray<uint32>& indices)
    {
        LocalIndexBuffer.InitAsCopyOf(indices);
        GpuIndexBuffer.Create(renderer::eGpuBufferType::IndexBuffer, Slice(indices));
    }

    /**
     * @brief Uploads mesh indices to a primtive mesh, and stores the indices without copy if the property
     * `KeepInMemory` is true.
     */
    void UploadIndices() { GpuIndexBuffer.Create<uint32>(renderer::eGpuBufferType::IndexBuffer, LocalIndexBuffer); }
    void SetIndices(SizedArray<uint32>&& indices) { LocalIndexBuffer = std::move(indices); }

    renderer::VertexList& GetVertices()
    {
        if (!bKeepInMemory) {
            LogWarning(LC_ASSET, "Requesting vertices from a primitive mesh while `KeepInMemory` != true!");
        }

        // Note that VertexList.LocalBuffer will be empty given bKeepInMemory is false.
        return VertexList;
    }

    SizedArray<uint32>& GetIndices()
    {
        if (!bKeepInMemory) {
            LogWarning(LC_ASSET, "Requesting indices from a primitive mesh while `KeepInMemory` != true!");
        }

        // This will return an empty array if `KeepInMemory` is false!
        return LocalIndexBuffer;
    }

    bool IsWritable() { return (VertexList.GpuBuffer.Initialized && GpuIndexBuffer.Initialized); }

    renderer::GpuBuffer& GetVertexBuffer() { return VertexList.GpuBuffer; }
    renderer::GpuBuffer& GetIndexBuffer() { return GpuIndexBuffer; }

    void Render(const renderer::CommandBuffer& cmd, uint32 num_instances)
    {
        const VkDeviceSize offset = 0;
        //        FrameData* frame = Fwd_GetFrame();

        if (VertexList.GpuBuffer.Buffer == nullptr) {
            FX_BREAKPOINT;
        }

        vkCmdBindVertexBuffers(cmd.Cmd, 0, 1, &VertexList.GpuBuffer.Buffer, &offset);
        vkCmdBindIndexBuffer(cmd.Cmd, GpuIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd.Cmd, static_cast<uint32>(GpuIndexBuffer.Size / sizeof(uint32)), num_instances, 0, 0, 0);
    }

    void RecalculateNormals()
    {
        using VertexType = renderer::Vertex<renderer::eVertexType::Default>;

        if (LocalIndexBuffer.IsEmpty()) {
            LogWarning(LC_ASSET, "Cannot recalculate normals as local indices are missing!");
            return;
        }

        AnonArray& vertices = VertexList.GetLocalBuffer();

        if (vertices.IsEmpty()) {
            LogWarning(LC_ASSET, "Cannot recalculate normals as local vertices are missing!");
            return;
        }

        const uint32 num_vertices = vertices.Size;
        const uint32 num_faces = num_vertices / 3;

        // Zero the mesh normals
        for (uint32 index = 0; index < num_vertices; index++) {
            memset(vertices.Get<VertexType>(index).Normal, 0, sizeof(VertexType::Normal));
        }

        for (uint32 index = 0; index < num_faces; index++) {
            // Indices for each vertex of the triangle
            const uint32 index_a = LocalIndexBuffer.pData[index];
            const uint32 index_b = LocalIndexBuffer.pData[index + 1];
            const uint32 index_c = LocalIndexBuffer.pData[index + 2];

            /*
                        A
                      / |
                    /   |
                  /     |
                B-------C

                Get the edge between A and B and the edge between C and B.
                then, we get the normal using:

                Normal = EdgeA x EdgeB.

                Average across vertices and normalize to remove scale.
             */


            VertexType& vertex_a = vertices.Get<VertexType>(index_a);
            VertexType& vertex_b = vertices.Get<VertexType>(index_b);
            VertexType& vertex_c = vertices.Get<VertexType>(index_c);

            const Vec3f edge_a = Vec3f::FromDifference(vertex_a.Position, vertex_b.Position);
            const Vec3f edge_b = Vec3f::FromDifference(vertex_c.Position, vertex_b.Position);

            const Vec3f normal = edge_a.Cross(edge_b);

            vertex_a.Normal[0] += normal.X;
            vertex_a.Normal[1] += normal.Y;
            vertex_a.Normal[2] += normal.Z;

            vertex_b.Normal[0] += normal.X;
            vertex_b.Normal[1] += normal.Y;
            vertex_b.Normal[2] += normal.Z;

            vertex_c.Normal[0] += normal.X;
            vertex_c.Normal[1] += normal.Y;
            vertex_c.Normal[2] += normal.Z;
        }


        for (uint32 index = 0; index < vertices.Size; index++) {
            VertexType& vertex = vertices.Get<VertexType>(index);

            Vec3f vec(vertex.Position);
            vec.NormalizeIP();
            vec.CopyTo(vertex.Normal);
        }

        VertexList.bContainsNormals = true;
    }

    void Destroy()
    {
        if (bIsReference || !bIsReady.load()) {
            return;
        }

        bIsReady.store(false);

        VertexList.Destroy();

        GpuIndexBuffer.Destroy();
        LocalIndexBuffer.Free();
    }

    ~PrimitiveMesh() { Destroy(); }


public:
    renderer::VertexList VertexList;

    SizedArray<MeshBone> Bones;

    std::atomic_bool bIsReady = std::atomic_bool(false);

    bool bIsReference = false;
    bool bKeepInMemory = false;

    renderer::GpuBuffer GpuIndexBuffer;
    SizedArray<uint32> LocalIndexBuffer;
};

} // namespace fx
