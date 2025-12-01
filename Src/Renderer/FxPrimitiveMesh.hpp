#pragma once

#include "Backend/Fwd/Rx_Fwd_GetFrame.hpp"
#include "Backend/RxFrameData.hpp"
#include "Backend/RxGpuBuffer.hpp"

#include <atomic>

template <typename TVertexType = RxVertexDefault>
class FxPrimitiveMesh
{
public:
    FxPrimitiveMesh() = default;

    FxPrimitiveMesh(FxSizedArray<TVertexType>& vertices) { UploadVertices(vertices); }

    FxPrimitiveMesh(FxSizedArray<TVertexType>& vertices, FxSizedArray<uint32>& indices)
    {
        CreateFromData(vertices, indices);
    }

    template <typename T>
    FxPrimitiveMesh(FxPrimitiveMesh<T>& other)
    {
        VertexList = other.VertexList;
        mGpuIndexBuffer = other.mGpuIndexBuffer;

        IsReady = other.IsReady;
    }

    // void CreateFromData(const FxSizedArray<FxVec3f>& vertices, FxSizedArray<uint32>& indices)
    //     requires std::same_as<TVertexType, RxVertex<FxVertexPosition>>
    // {
    //     UploadVertices(static_cast<FxSizedArray<TVertexType>>(vertices));
    //     UploadIndices(indices);
    // }

    /**
     * @brief Uploads the vertices and indices to the primitive mesh. Copies the vertices and indices into
     * a cpu-side buffer if the property `KeepInMemory` is true.
     */
    // void CreateFromData(const FxSizedArray<TVertexType>& vertices, const FxSizedArray<uint32>& indices)
    // {
    //     UploadVertices(vertices);
    //     UploadIndices(indices);
    // }

    /**
     * @brief Calls `.UploadToGpu()` on the vertex list. Assumes that the vertex list has been modified with vertices.
     *
     */
    inline void UploadVertices()
    {
        if constexpr (std::is_same_v<TVertexType, RxVertexDefault>) {
            if (!VertexList.bContainsNormals) {
                FxLogDebug("Recalculating normals for mesh");
                RecalculateNormals();
            }
        }


        VertexList.UploadToGpu();
    }

    /**
     * @brief Uploads the vertices and indices to the primitive mesh. Moves the vertices (no copy)
     * into a cpu-side buffer if the property `KeepInMemory` is true.
     */
    void UploadVertices(const FxSizedArray<TVertexType>& vertices)
    {
        if (KeepInMemory) {
            VertexList.LocalBuffer.InitAsCopyOf(vertices);
        }

        UploadVertices();
    }

    /**
     * @brief Uploads mesh vertices to a primtive mesh, and stores the vertices without copy if the property
     * `KeepInMemory` is true.
     */
    void UploadVertices(FxSizedArray<TVertexType>&& vertices)
    {
        if (KeepInMemory) {
            VertexList.LocalBuffer = std::move(vertices);
        }

        UploadVertices();
    }

    /** @brief Uploads mesh indices to a primitive mesh. */
    void UploadIndices(const FxSizedArray<uint32>& indices)
    {
        // if (KeepInMemory) {
        mLocalIndexBuffer.InitAsCopyOf(indices);
        // }

        mGpuIndexBuffer.Create(RxBufferUsageType::Indices, indices);
    }

    /**
     * @brief Uploads mesh indices to a primtive mesh, and stores the indices without copy if the property
     * `KeepInMemory` is true.
     */
    void UploadIndices(FxSizedArray<uint32>&& indices)
    {
        mGpuIndexBuffer.Create(RxBufferUsageType::Indices, indices);

        // if (KeepInMemory) {
        mLocalIndexBuffer = std::move(indices);
        // }
    }

    FxSizedArray<TVertexType>& GetVertices()
    {
        if (!KeepInMemory) {
            FxLogWarning("Requesting vertices from a primitive mesh while `KeepInMemory` != true!");
        }

        // This will return an empty FxSizedArray if `KeepInMemory` is false!
        return VertexList.LocalBuffer;
    }

    FxSizedArray<uint32>& GetIndices()
    {
        if (!KeepInMemory) {
            FxLogWarning("Requesting indices from a primitive mesh while `KeepInMemory` != true!");
        }

        // This will return an empty FxSizedArray if `KeepInMemory` is false!
        return mLocalIndexBuffer;
    }

    bool IsWritable() { return (VertexList.GpuBuffer.Initialized && mGpuIndexBuffer.Initialized); }

    RxGpuBuffer<TVertexType>& GetVertexBuffer() { return VertexList.GpuBuffer; }
    RxGpuBuffer<uint32>& GetIndexBuffer() { return mGpuIndexBuffer; }

    void Render(const RxCommandBuffer& cmd, const RxGraphicsPipeline& pipeline)
    {
        const VkDeviceSize offset = 0;
        //        RxFrameData* frame = Rx_Fwd_GetFrame();

        vkCmdBindVertexBuffers(cmd.CommandBuffer, 0, 1, &VertexList.GpuBuffer.Buffer, &offset);
        vkCmdBindIndexBuffer(cmd.CommandBuffer, mGpuIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd.CommandBuffer, static_cast<uint32>(mGpuIndexBuffer.Size), 1, 0, 0, 0);
    }

    void RecalculateNormals()
    {
        if (mLocalIndexBuffer.IsEmpty()) {
            FxLogWarning("Cannot recalculate normals as a local indices are missing!");
            return;
        }

        if (VertexList.LocalBuffer.IsEmpty()) {
            FxLogWarning("Cannot recalculate normals as a local vertices are missing!");
            return;
        }

        FxSizedArray<RxVertexDefault>& vertices = VertexList.LocalBuffer;

        const uint32 num_vertices = vertices.Size;
        const uint32 num_faces = num_vertices / 3;

        // Zero the mesh normals
        for (uint32 index = 0; index < num_vertices; index++) {
            memset(vertices[index].Normal, 0, sizeof(float32) * 3);
        }

        for (uint32 index = 0; index < num_faces; index++) {
            // Indices for each vertex of the triangle
            const uint32 index_a = mLocalIndexBuffer.pData[index];
            const uint32 index_b = mLocalIndexBuffer.pData[index + 1];
            const uint32 index_c = mLocalIndexBuffer.pData[index + 2];

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


            RxVertexDefault& vertex_a = vertices.pData[index_a];
            RxVertexDefault& vertex_b = vertices.pData[index_b];
            RxVertexDefault& vertex_c = vertices.pData[index_c];

            const FxVec3f edge_a = FxVec3f::FromDifference(vertex_a.Position, vertex_b.Position);
            const FxVec3f edge_b = FxVec3f::FromDifference(vertex_c.Position, vertex_b.Position);

            const FxVec3f normal = edge_a.Cross(edge_b);

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


        for (uint32 index = 0; index < VertexList.LocalBuffer.Size; index++) {
            RxVertexDefault& vertex = VertexList.LocalBuffer[index];

            FxVec3f vec(vertex.Position);
            vec.NormalizeIP();
            vec.CopyTo(vertex.Normal);
        }

        VertexList.bContainsNormals = true;
    }


    //    void Render(RxCommandBuffer& cmd, RxGraphicsPipeline& pipeline)
    //    {
    //        const VkDeviceSize offset = 0;
    //        vkCmdBindVertexBuffers(cmd.CommandBuffer, 0, 1, &mVertexBuffer.Buffer, &offset);
    //        vkCmdBindIndexBuffer(cmd.CommandBuffer, mIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
    //
    //        vkCmdDrawIndexed(cmd.CommandBuffer, static_cast<uint32>(mIndexBuffer.Size), 1, 0, 0, 0);
    //    }

    void Destroy()
    {
        if (IsReference || !IsReady.load()) {
            return;
        }

        IsReady.store(false);

        VertexList.Destroy();

        mGpuIndexBuffer.Destroy();
        mLocalIndexBuffer.Free();
    }

    ~FxPrimitiveMesh() { Destroy(); }


public:
    RxVertexList<TVertexType> VertexList;

    std::atomic_bool IsReady = std::atomic_bool(false);

    bool IsReference = false;
    bool KeepInMemory = false;

protected:
    RxGpuBuffer<uint32> mGpuIndexBuffer;
    FxSizedArray<uint32> mLocalIndexBuffer;
};
