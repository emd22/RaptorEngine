#pragma once

#include "Fwd/Rx_Fwd_AddToDeletionQueue.hpp"
#include "Fwd/Rx_Fwd_GetGpuAllocator.hpp"
#include "Fwd/Rx_Fwd_SubmitUploadGpuCmd.hpp"

#include <memory.h>

#include <Core/FxPanic.hpp>
#include <Core/FxTypes.hpp>
#include <Core/FxUtil.hpp>
#include <cstring>

// #define FX_DEBUG_GPU_BUFFER_ALLOCATION_NAMES 1

class RxRawGpuBuffer;
class RxGpuBuffer;

enum class RxBufferUsageType
{
    Vertices = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    Indices = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
};

enum class RxGpuBufferFlags : uint16
{
    eNone = 0x00,
    /** The buffer is mapped for the lifetime of the buffer. */
    ePersistentMapped = 0x01,

    FX_DEFINE_AS_FLAG_ENUM,
};

// FX_DEFINE_ENUM_AS_FLAGS(RxGpuBufferFlags);


/**
 * @brief Provides a GPU buffer that can be created with more complex parameters without staging.
 */
class RxRawGpuBuffer
{
public:
    RxRawGpuBuffer() = default;

    RxRawGpuBuffer(RxRawGpuBuffer& other) = delete;

    RxRawGpuBuffer operator=(RxRawGpuBuffer& other) = delete;

    void Create(uint64 size_in_bytes, VkBufferUsageFlags buffer_usage, VmaMemoryUsage memory_usage,
                RxGpuBufferFlags buffer_flags = RxGpuBufferFlags::eNone)
    {
        Size = size_in_bytes;
        mUsageFlags = buffer_usage;
        mBufferFlags = buffer_flags;

        VmaAllocationCreateFlags vma_create_flags = 0;

        if ((mBufferFlags & RxGpuBufferFlags::ePersistentMapped) != 0) {
            vma_create_flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        const VkBufferCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                 .flags = 0,
                                                 .size = size_in_bytes,
                                                 .usage = mUsageFlags,
                                                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE };

        const VmaAllocationCreateInfo alloc_create_info = { .flags = vma_create_flags, .usage = memory_usage };

        VmaAllocationInfo allocation_info;
        const VkResult status = vmaCreateBuffer(Fx_Fwd_GetGpuAllocator(), &create_info, &alloc_create_info, &Buffer,
                                                &Allocation, &allocation_info);

        if (status != VK_SUCCESS) {
            FxPanicVulkan("GPUBuffer", "Error allocating staging buffer!", status);
        }


        if ((mBufferFlags & RxGpuBufferFlags::ePersistentMapped) != 0) {
            // Get the pointer from VMA for the mapped GPU buffer
            pMappedBuffer = allocation_info.pMappedData;
        }

        Initialized = true;
    }

    void FlushToGpu(uint32 offset, uint32 size)
    {
        vmaFlushAllocation(Fx_Fwd_GetGpuAllocator(), Allocation, offset, size);
    }

    void Map()
    {
        if (IsMapped()) {
            FxLogWarning("Buffer {:p} is already mapped!", reinterpret_cast<void*>(Buffer));
            return;
        }

        const VkResult status = vmaMapMemory(Fx_Fwd_GetGpuAllocator(), Allocation, &pMappedBuffer);

        if (status != VK_SUCCESS) {
            FxLogError("Could not map GPU memory to main memory! (Usage: 0x{:x})", mUsageFlags);
            return;
        }
    }

    bool IsMapped() const { return pMappedBuffer != nullptr; }

    void UnMap()
    {
        if (!IsMapped()) {
            return;
        }

        vmaUnmapMemory(Fx_Fwd_GetGpuAllocator(), Allocation);
        pMappedBuffer = nullptr;
    }

    template <typename TElementType>
    void Upload(const FxSizedArray<TElementType>& data)
    {
        FxDebugAssert(data.Size > 0);
        FxDebugAssert(data.pData != nullptr);

        FxAssertMsg(this->Size >= data.GetSizeInBytes(), "GPU buffer is smaller than source buffer!");

        Map();

        const size_t size_in_bytes = data.GetSizeInBytes();
        memcpy(pMappedBuffer, data.pData, size_in_bytes);

        UnMap();
    }

    void Destroy()
    {
        // FxLogInfo("Destroying GPU buffer of size {}", Size);
        if (!(Initialized.load()) || Allocation == nullptr || Buffer == nullptr) {
            return;
        }

        // if (IsMapped()) {
        //     UnMap();
        // }

        Fx_Fwd_AddGpuBufferToDeletionQueue(this->Buffer, this->Allocation);

        Initialized.store(false);
        Size = 0;
    }

    ~RxRawGpuBuffer() { Destroy(); }

public:
    VkBuffer Buffer = nullptr;
    VmaAllocation Allocation = nullptr;

    void* pMappedBuffer = nullptr;

    std::atomic_bool Initialized = false;
    uint64 Size = 0;

private:
    VkBufferUsageFlags mUsageFlags = 0;
    RxGpuBufferFlags mBufferFlags = RxGpuBufferFlags::eNone;
};


class RxGpuBufferMapContext
{
public:
    RxGpuBufferMapContext(RxRawGpuBuffer* buffer) : mpGpuBuffer(buffer) { mpGpuBuffer->Map(); }

    /** Returns the raw pointer representation of the mapped data. */
    operator void*()
    {
        if (!mpGpuBuffer->pMappedBuffer) {
            return nullptr;
        }

        return mpGpuBuffer->pMappedBuffer;
    }

    /** Returns the pointer representation of the mapped data. */
    template <typename TElementType>
    TElementType* GetPtr()
    {
        FxDebugAssert(mpGpuBuffer->pMappedBuffer != nullptr);
        return static_cast<TElementType*>(mpGpuBuffer->pMappedBuffer);
    }

    ~RxGpuBufferMapContext() { mpGpuBuffer->UnMap(); }

    /**
     * Manually unmaps the buffer.
     */
    void UnMap() const { mpGpuBuffer->UnMap(); }

private:
    RxRawGpuBuffer* mpGpuBuffer = nullptr;
};


/**
 * A GPU buffer that is created CPU side, and copied over to a GPU-only buffer. This is the default
 * buffer type.
 */
class RxGpuBuffer : public RxRawGpuBuffer
{
private:
    using RxRawGpuBuffer::Create;

public:
    RxGpuBuffer() = default;

    template <typename TElementType>
    void Create(RxBufferUsageType usage, const FxSizedArray<TElementType>& data)
    {
        this->Size = data.Size * sizeof(TElementType);
        Usage = usage;

        RxRawGpuBuffer staging_buffer;
        staging_buffer.Create(Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        // Upload the data to the staging buffer
        staging_buffer.Upload(data);

        this->Create(this->Size, FxUtil::EnumToInt(Usage) | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VMA_MEMORY_USAGE_GPU_ONLY);

        Fx_Fwd_SubmitUploadCmd(
            [&](RxCommandBuffer& cmd)
            {
                VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = Size };
                vkCmdCopyBuffer(cmd.CommandBuffer, staging_buffer.Buffer, this->Buffer, 1, &copy);
            });

        staging_buffer.Destroy();
    }

public:
    RxBufferUsageType Usage;
};
