#pragma once

#include "Backend/Fwd/Rx_Fwd_GetFrame.hpp"
#include "Backend/RxFrameData.hpp"
#include "Backend/RxGpuBuffer.hpp"

#include <atomic>

template <typename TVertexType = RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>
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
        mGpuVertexBuffer = other.mGpuVertexBuffer;
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
    void CreateFromData(const FxSizedArray<TVertexType>& vertices, const FxSizedArray<uint32>& indices)
    {
        UploadVertices(vertices);
        UploadIndices(indices);
    }

    /**
     * @brief Uploads the vertices and indices to the primitive mesh. Moves the vertices and indices (no copy)
     * into a cpu-side buffer if the property `KeepInMemory` is true.
     */
    void CreateFromData(FxSizedArray<TVertexType>&& vertices, FxSizedArray<uint32>&& indices)
    {
        UploadVertices(vertices);
        UploadIndices(indices);
    }

    /** @brief Uploads mesh vertices to a primitve mesh. */
    void UploadVertices(const FxSizedArray<TVertexType>& vertices)
    {
        if (KeepInMemory) {
            mVertexBuffer.InitAsCopyOf(vertices);
        }

        mGpuVertexBuffer.Create(RxBufferUsageType::Vertices, vertices);
    }

    /**
     * @brief Uploads mesh vertices to a primtive mesh, and stores the vertices without copy if the property
     * `KeepInMemory` is true.
     */
    void UploadVertices(FxSizedArray<TVertexType>&& vertices)
    {
        mGpuVertexBuffer.Create(RxBufferUsageType::Vertices, vertices);

        if (KeepInMemory) {
            mVertexBuffer = std::move(vertices);
        }
    }

    /** @brief Uploads mesh indices to a primitive mesh. */
    void UploadIndices(const FxSizedArray<uint32>& indices)
    {
        if (KeepInMemory) {
            mIndexBuffer.InitAsCopyOf(indices);
        }

        mGpuIndexBuffer.Create(RxBufferUsageType::Indices, indices);
    }

    /**
     * @brief Uploads mesh indices to a primtive mesh, and stores the indices without copy if the property
     * `KeepInMemory` is true.
     */
    void UploadIndices(FxSizedArray<uint32>&& indices)
    {
        mGpuIndexBuffer.Create(RxBufferUsageType::Indices, indices);

        if (KeepInMemory) {
            mIndexBuffer = std::move(indices);
        }
    }


    static FxSizedArray<TVertexType> MakeCombinedVertexBuffer(const FxSizedArray<float32>& positions,
                                                              const FxSizedArray<float32>& normals,
                                                              const FxSizedArray<float32>& uvs)
        requires std::same_as<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>
    {
        FxAssert((normals.Size == positions.Size));

        FxSizedArray<TVertexType> vertices(positions.Size / 3);

        // Log::Info("Creating combined vertex buffer (s: %d)", vertices.Capacity);
        const bool has_texcoords = uvs.Size > 0;

        if (!has_texcoords) {
            OldLog::Info("Model does not have texture coordinates!", 0);
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

    FxSizedArray<TVertexType>& GetVertices()
    {
        if (!KeepInMemory) {
            FxLogWarning("Requesting vertices from a primitive mesh while `KeepInMemory` != true!");
        }

        // This will return an empty FxSizedArray if `KeepInMemory` is false!
        return mVertexBuffer;
    }

    FxSizedArray<uint32>& GetIndices()
    {
        if (!KeepInMemory) {
            FxLogWarning("Requesting indices from a primitive mesh while `KeepInMemory` != true!");
        }

        // This will return an empty FxSizedArray if `KeepInMemory` is false!
        return mIndexBuffer;
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

        constexpr bool contains_uv =
            std::is_same_v<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>;
        constexpr bool contains_normal = std::is_same_v<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal>> ||
                                         contains_uv;

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

        constexpr bool contains_uv =
            std::is_same_v<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>>;
        constexpr bool contains_normal = std::is_same_v<TVertexType, RxVertex<FxVertexPosition | FxVertexNormal>> ||
                                         contains_uv;

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


    bool IsWritable() { return (mGpuVertexBuffer.Initialized && mGpuIndexBuffer.Initialized); }

    RxGpuBuffer<TVertexType>& GetVertexBuffer() { return mGpuVertexBuffer; }
    RxGpuBuffer<uint32>& GetIndexBuffer() { return mGpuIndexBuffer; }

    void Render(const RxCommandBuffer& cmd, const RxGraphicsPipeline& pipeline)
    {
        const VkDeviceSize offset = 0;
        //        RxFrameData* frame = Rx_Fwd_GetFrame();

        vkCmdBindVertexBuffers(cmd.CommandBuffer, 0, 1, &mGpuVertexBuffer.Buffer, &offset);
        vkCmdBindIndexBuffer(cmd.CommandBuffer, mGpuIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd.CommandBuffer, static_cast<uint32>(mGpuIndexBuffer.Size), 1, 0, 0, 0);
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
        if (IsReference) {
            return;
        }

        if (IsReady.load() == false) {
            return;
        }

        IsReady.store(false);

        mGpuVertexBuffer.Destroy();
        mGpuIndexBuffer.Destroy();

        mVertexBuffer.Free();
        mIndexBuffer.Free();
    }

    ~FxPrimitiveMesh() { Destroy(); }

    std::atomic_bool IsReady = std::atomic_bool(false);

    bool IsReference = false;
    bool KeepInMemory = false;

protected:
    RxGpuBuffer<TVertexType> mGpuVertexBuffer;
    RxGpuBuffer<uint32> mGpuIndexBuffer;

    /* CPU memory vertex and index buffers */
    FxSizedArray<TVertexType> mVertexBuffer;
    FxSizedArray<uint32> mIndexBuffer;
};
