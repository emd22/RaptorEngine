#pragma once

#include "Backend/RxFrameData.hpp"
#include "Backend/RxGpuBuffer.hpp"
#include "Renderer/Renderer.hpp"

#include <atomic>

template <typename TVertexType = RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>
class FxMesh
{
public:
    FxMesh() = default;

    FxMesh(FxSizedArray<TVertexType>& vertices)
    {
        UploadVertices(vertices);
    }

    FxMesh(FxSizedArray<TVertexType>& vertices, FxSizedArray<uint32>& indices)
    {
        CreateFromData(vertices, indices);
    }

    template <typename T>
    FxMesh(FxMesh<T>& other)
    {
        mVertexBuffer = other.mVertexBuffer;
        mIndexBuffer = other.mIndexBuffer;

        IsReady = other.IsReady;
    }

    // void CreateFromData(const FxSizedArray<FxVec3f>& vertices, FxSizedArray<uint32>& indices)
    //     requires std::same_as<TVertexType, RxVertex<FxVertexPosition>>
    // {
    //     UploadVertices(static_cast<FxSizedArray<TVertexType>>(vertices));
    //     UploadIndices(indices);
    // }

    void CreateFromData(FxSizedArray<TVertexType>& vertices, FxSizedArray<uint32>& indices)
    {
        UploadVertices(vertices);
        UploadIndices(indices);
    }

    void UploadVertices(FxSizedArray<TVertexType>& vertices)
    {
        mVertexBuffer.Create(RxBufferUsageType::Vertices, vertices);
    }

    void UploadIndices(FxSizedArray<uint32>& indices)
    {
        mIndexBuffer.Create(RxBufferUsageType::Indices, indices);
    }

    static FxSizedArray<TVertexType> MakeCombinedVertexBuffer(
        const FxSizedArray<float32>& positions,
        const FxSizedArray<float32>& normals,
        const FxSizedArray<float32>& uvs
    ) requires std::same_as<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>
    {
        FxAssert((normals.Size == positions.Size));

        FxSizedArray<TVertexType> vertices(positions.Size / 3);

        // Log::Info("Creating combined vertex buffer (s: %d)", vertices.Capacity);
        const bool has_texcoords = uvs.Size > 0;

        if (!has_texcoords) {
            Log::Info("Model does not have texture coordinates!", 0);
        }

        for (int i = 0; i < vertices.Capacity; i++) {
            TVertexType vertex;

            memcpy(&vertex.Position, &positions.Data[i * 3], sizeof(float32) * 3);
            memcpy(&vertex.Normal, &normals.Data[i * 3], sizeof(float32) * 3);

            if (has_texcoords) {
                memcpy(&vertex.UV, &uvs.Data[i * 2], sizeof(float32) * 2);
            }
            else {
                memset(&vertex.UV, 0, sizeof(float32) * 2);
            }

            vertices.Insert(vertex);
        }

        return vertices;
    }

    /**
     * Makes a combined vertex buffer (Contains a number of components) from a list of positions.
     * This function zeros out any potential normals or UVs given `TVertexType` includes them.
     *
     * Positions {1, 2, 3, 4, 5, 6} -> Vertex 1: {1, 2, 3}, Vertex 2: {4, 5, 6}.
     *
     * @param positions The positions to create the vertex buffer from.
     */
    static FxSizedArray<TVertexType> MakeCombinedVertexBuffer(const FxSizedArray<float32>& positions)
    {
        const uint32 vertex_count = positions.Size / 3;

        constexpr bool contains_uv = std::is_same_v<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>;
        constexpr bool contains_normal = std::is_same_v<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal>> || contains_uv;

        FxSizedArray<TVertexType> vertices(vertex_count);

        for (uint32 i = 0; i < vertex_count; i++) {
            TVertexType vertex;
            memcpy(&vertex.Position, &positions.Data[i * 3], sizeof(float32) * 3);

            if constexpr (contains_normal) {
                memset(&vertex.Normal, 0, sizeof(float32) * 3);
            }

            if constexpr (contains_uv) {
                memset(&vertex.UV, 0, sizeof(float32) * 2);
            }

            vertices.Insert(vertex);
        }

        return vertices;
    }

    static FxSizedArray<TVertexType> MakeCombinedVertexBuffer(const FxSizedArray<FxVec3f>& positions)
    {
        const uint32 vertex_count = positions.Size;

        constexpr bool contains_uv = std::is_same_v<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>;
        constexpr bool contains_normal = std::is_same_v<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal>> || contains_uv;

        FxSizedArray<TVertexType> vertices(vertex_count);

        for (uint32 i = 0; i < vertex_count; i++) {
            TVertexType vertex;
            memcpy(&vertex.Position, positions.Data[i].mData, sizeof(float32) * 3);

            if constexpr (contains_normal) {
                memset(&vertex.Normal, 0, sizeof(float32) * 3);
            }

            if constexpr (contains_uv) {
                memset(&vertex.UV, 0, sizeof(float32) * 2);
            }

            vertices.Insert(vertex);
        }

        return vertices;
    }


    bool IsWritable()
    {
        return (mVertexBuffer.Initialized && mIndexBuffer.Initialized);
    }

    RxGpuBuffer<TVertexType> &GetVertexBuffer() { return mVertexBuffer; }
    RxGpuBuffer<uint32> &GetIndexBuffer() { return mIndexBuffer; }

    void Render(RxGraphicsPipeline& pipeline)
    {
        const VkDeviceSize offset = 0;
        RxFrameData* frame = Renderer->GetFrame();

        vkCmdBindVertexBuffers(frame->CommandBuffer.CommandBuffer, 0, 1, &mVertexBuffer.Buffer, &offset);
        vkCmdBindIndexBuffer(frame->CommandBuffer.CommandBuffer, mIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(frame->CommandBuffer.CommandBuffer, static_cast<uint32>(mIndexBuffer.Size), 1, 0, 0, 0);
    }

    void Render(RxCommandBuffer& cmd, RxGraphicsPipeline& pipeline)
    {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd.CommandBuffer, 0, 1, &mVertexBuffer.Buffer, &offset);
        vkCmdBindIndexBuffer(cmd.CommandBuffer, mIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd.CommandBuffer, static_cast<uint32>(mIndexBuffer.Size), 1, 0, 0, 0);
    }

    void Destroy()
    {
        if (IsReference) {
            return;
        }

        if (IsReady.load() == false) {
            return;
        }

        IsReady.store(false);

        mVertexBuffer.Destroy();
        mIndexBuffer.Destroy();
    }

    ~FxMesh()
    {
        Destroy();
    }

    std::atomic_bool IsReady = std::atomic_bool(false);

    bool IsReference = false;

protected:
    RxGpuBuffer<TVertexType> mVertexBuffer;
    RxGpuBuffer<uint32> mIndexBuffer;
};
