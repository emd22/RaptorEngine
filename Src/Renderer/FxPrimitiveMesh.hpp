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
    inline void UploadVertices() { VertexList.UploadToGpu(); }

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
        if (KeepInMemory) {
            mLocalIndexBuffer.InitAsCopyOf(indices);
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
            mLocalIndexBuffer = std::move(indices);
        }
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

        VertexList.Destroy();

        IsReady.store(false);

        // mGpuVertexBuffer.Destroy();
        mGpuIndexBuffer.Destroy();

        // mVertexBuffer.Free();
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
