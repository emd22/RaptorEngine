#pragma once

#include "Core/FxUtil.hpp"
#include <Core/Types.hpp>
#include <Core/FxPanic.hpp>

#include "Fwd/Fwd_SubmitUploadGpuCmd.hpp"
#include "Fwd/Fwd_AddToDeletionQueue.hpp"
#include "Fwd/Fwd_GetGpuAllocator.hpp"

#include <cstring>
#include <memory.h>

#define FX_DEBUG_GPU_BUFFER_ALLOCATION_NAMES 1

template <typename ElementType>
class RxRawGpuBuffer;

template <typename ElementType>
class RxGpuBuffer;

enum class RxBufferUsageType {
    Vertices = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    Indices = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
};

template <typename ElementType>
class RxGpuBufferMapContext
{
public:

    RxGpuBufferMapContext(RxRawGpuBuffer<ElementType> *buffer)
        : mGpuBuffer(buffer)
    {
        mGpuBuffer->Map();
    }

    /** Returns the raw pointer representation of the mapped data. */
    operator void *()
    {
        if (!mGpuBuffer->MappedBuffer) {
            return nullptr;
        }

        return mGpuBuffer->MappedBuffer;
    }

    /** Returns the pointer representation of the mapped data. */
    ElementType *GetPtr()
    {
        FxDebugAssert(mGpuBuffer->MappedBuffer != nullptr);
        return static_cast<ElementType *>(mGpuBuffer->MappedBuffer);
    }

    ~RxGpuBufferMapContext()
    {
        mGpuBuffer->UnMap();
    }

    /**
     * Manually unmaps the buffer.
     */
    void UnMap() const
    {
        mGpuBuffer->UnMap();
    }

private:
    RxRawGpuBuffer<ElementType> *mGpuBuffer = nullptr;
};


/**
 * Provides a GPU buffer that can be created with more complex parameters without staging.
 */
template <typename ElementType>
class RxRawGpuBuffer
{
public:
    const int32 ElementSize = sizeof(ElementType);

public:
    RxRawGpuBuffer() = default;

    RxRawGpuBuffer(RxRawGpuBuffer<ElementType> &other) = delete;

    RxRawGpuBuffer operator = (RxRawGpuBuffer<ElementType> &other) = delete;

    void Create(uint64 element_count, VkBufferUsageFlags buffer_usage, VmaMemoryUsage memory_usage)
    {
        Size = element_count;
        mUsageFlags = buffer_usage;

        const uint64 buffer_size = ElementSize * Size;

        const VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buffer_size,
            .usage = mUsageFlags,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .flags = 0
        };

        const VmaAllocationCreateInfo alloc_create_info = {
            .usage = memory_usage,
        };


        const VkResult status = vmaCreateBuffer(
            Fx_Fwd_GetGpuAllocator(),
            &create_info,
            &alloc_create_info,
            &Buffer,
            &Allocation,
            nullptr
        );

        if (status != VK_SUCCESS) {
            FxPanic("GPUBuffer", "Error allocating staging buffer!", status);
        }

    #ifdef FX_DEBUG_GPU_BUFFER_ALLOCATION_NAMES
        static uint32 allocation_number = 0;

        allocation_number += 1;

        std::string allocation_name = "";
        // typeid(ElementType).name();

        char name_buffer[256];
        char demangled_name_buffer[128];

        FxUtil::DemangleName(typeid(ElementType).name(), demangled_name_buffer, 128);

        snprintf(name_buffer, 128, "%s{%u}", demangled_name_buffer, allocation_number);

        vmaSetAllocationName(Fx_Fwd_GetGpuAllocator(), Allocation, name_buffer);
    #endif

        Initialized = true;
    }


    /**
     * Creates a new context that automatically maps and unmaps memory at the
     * end of scope.
     *
     * To get the mapped buffer, either use `value.MappedBuffer`, or `value` when using the value as a pointer.
     */
    RxGpuBufferMapContext<ElementType> GetMappedContext()
    {
        return RxGpuBufferMapContext<ElementType>(this);
    }

    void Map()
    {
        if (IsMapped()) {
            Log::Warning("Buffer %p is already mapped!", Buffer);
            return;
        }

        const VkResult status = vmaMapMemory(Fx_Fwd_GetGpuAllocator(), Allocation, &MappedBuffer);

        if (status != VK_SUCCESS) {
            Log::Error("Could not map GPU memory to main memory! (Usage: 0x%X)", mUsageFlags);
            return;
        }
    }

    bool IsMapped() const
    {
        return MappedBuffer != nullptr;
    }

    void UnMap()
    {
        if (!IsMapped()) {
            return;
        }

        vmaUnmapMemory(Fx_Fwd_GetGpuAllocator(), Allocation);
        MappedBuffer = nullptr;
    }

    void Upload(const FxSizedArray<ElementType> &data)
    {
        FxDebugAssert(data.Size > 0);
        FxDebugAssert(data.Data != nullptr);

        auto buffer = GetMappedContext();
        const size_t size_in_bytes = data.GetSizeInBytes();
        memcpy(buffer.GetPtr(), data.Data, size_in_bytes);
    }

    void Destroy()
    {
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

    ~RxRawGpuBuffer()
    {
        Destroy();
    }

public:
    VkBuffer Buffer = nullptr;
    VmaAllocation Allocation = nullptr;

    void *MappedBuffer = nullptr;

    std::atomic_bool Initialized = false;
    uint64 Size = 0;
private:
    VkBufferUsageFlags mUsageFlags = 0;
};


/**
 * A GPU buffer that is created CPU side, and copied over to a GPU-only buffer. This is the default
 * buffer type.
 */
template <typename ElementType>
class RxGpuBuffer : public RxRawGpuBuffer<ElementType>
{
private:
    using RxRawGpuBuffer<ElementType>::Create;
public:
    RxGpuBuffer() = default;

    void Create(RxBufferUsageType usage, FxSizedArray<ElementType> &data)
    {
        this->Size = data.Size;
        Usage = usage;

        const uint64_t buffer_size = data.Size * sizeof(ElementType);

        RxRawGpuBuffer<ElementType> staging_buffer;
        staging_buffer.Create(this->Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        // Upload the data to the staging buffer
        staging_buffer.Upload(data);

        this->Create(this->Size, FxUtil::EnumToInt(Usage) | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        Fx_Fwd_SubmitUploadCmd([&](RxCommandBuffer &cmd) {
            VkBufferCopy copy = {
                .dstOffset = 0,
                .srcOffset = 0,
                .size = buffer_size
            };
            vkCmdCopyBuffer(cmd.CommandBuffer, staging_buffer.Buffer, this->Buffer, 1, &copy);
        });

        staging_buffer.Destroy();
    }
public:
    RxBufferUsageType Usage;
private:
    size_t ElementSize = sizeof(ElementType);
};
