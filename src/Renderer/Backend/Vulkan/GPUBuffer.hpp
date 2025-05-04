#pragma once

#include "Core/Util.hpp"
#include "Renderer/FxRenderBackend.hpp"
#include "Renderer/Renderer.hpp"
#include "vulkan/vulkan_core.h"
#include <ThirdParty/vk_mem_alloc.h>
#include <Core/Types.hpp>
#include <cstring>
#include <vulkan/vulkan.h>

#include <Core/FxPanic.hpp>

#include <memory.h>

namespace vulkan {


template <typename ElementType>
class FxRawGPUBuffer;

template <typename ElementType>
class FxStagedGPUBuffer;

enum class FxBufferUsageType {
    Vertices = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    Indices = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
};

template <typename ElementType>
class FxGPUBufferMapContext
{
public:

    FxGPUBufferMapContext(FxStagedGPUBuffer<ElementType> *buffer)
        : mGPUBuffer(buffer)
    {
        Map();
    }

    FxGPUBufferMapContext(FxRawGPUBuffer<ElementType> *buffer)
        : mRawGPUBuffer(buffer)
    {
        Map();
    }

    operator void *() const
    {
        return MappedBuffer;
    }

    void UnMap()
    {
        VmaAllocation allocation = GetAllocation();

        if (MappedBuffer != nullptr) {
            vmaUnmapMemory(RendererVulkan->GPUAllocator, allocation);
        }
        MappedBuffer = nullptr;
    }

    ~FxGPUBufferMapContext()
    {
        UnMap();
    }

private:
    VmaAllocation GetAllocation()
    {
        if (mGPUBuffer != nullptr) {
            return mGPUBuffer->Allocation;
        }
        else if (mRawGPUBuffer != nullptr) {
            return mRawGPUBuffer->Allocation;
        }

        FxPanic_("GPUBuffer", "No GPU buffer or raw GPU buffer available to map!", 0);
        return nullptr;
    }

    void Map()
    {
        VmaAllocation allocation = GetAllocation();

        const VkResult status = vmaMapMemory(RendererVulkan->GPUAllocator, allocation, &MappedBuffer);

        if (status != VK_SUCCESS) {
            Log::Error("Could not map GPU memory to main memory! (Usage: 0x%X)", EnumToInt(mGPUBuffer->Usage));
            return;
        }
    }

public:
    void *MappedBuffer = nullptr;

private:
    FxStagedGPUBuffer<ElementType> *mGPUBuffer = nullptr;
    FxRawGPUBuffer<ElementType> *mRawGPUBuffer = nullptr;
};


template <typename ElementType>
class FxRawGPUBuffer
{
public:
    const int32 ElementSize = sizeof(ElementType);

public:
    FxRawGPUBuffer() = default;

    FxRawGPUBuffer(FxRawGPUBuffer<ElementType> &other) = delete;

    FxRawGPUBuffer operator = (FxRawGPUBuffer<ElementType> &other) = delete;

    void Create(uint64 element_count, VkBufferUsageFlags buffer_usage, VmaMemoryUsage memory_usage)
    {
        Size = element_count;
        const uint64 buffer_size = ElementSize * element_count;

        const VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buffer_size,
            .usage = buffer_usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .flags = 0
        };

        const VmaAllocationCreateInfo alloc_create_info = {
            .usage = memory_usage,
        };

        const VkResult status = vmaCreateBuffer(
            RendererVulkan->GPUAllocator,
            &create_info,
            &alloc_create_info,
            &Buffer,
            &Allocation,
            nullptr
        );

        if (status != VK_SUCCESS) {
            FxPanic_("GPUBuffer", "Error allocating staging buffer!", status);
        }

        Initialized = true;
    }


    /**
     * Creates a new context that automatically maps and unmaps memory at the
     * end of scope.
     *
     * To get the mapped buffer, either use `value.MappedBuffer`, or `value` when using the value as a pointer.
     */
    FxGPUBufferMapContext<ElementType> GetMappedContext()
    {
        return FxGPUBufferMapContext<ElementType>(this);
    }

    void Destroy()
    {
        if (!Initialized || Allocation == nullptr || Buffer == nullptr) {
            return;
        }

        Renderer->AddGPUBufferToDeletionQueue([](FxDeletionObject *object) {
            vmaDestroyBuffer(RendererVulkan->GPUAllocator, object->Buffer, object->Allocation);
            Log::Debug("Deleted Raw VMA buffer");
        }, this->Buffer, this->Allocation);

        Initialized = false;
    }

    ~FxRawGPUBuffer()
    {
        Destroy();
    }

public:
    VkBuffer Buffer = nullptr;
    VmaAllocation Allocation = nullptr;

    std::atomic_bool Initialized = false;

    uint64 Size = 0;
};

template <typename ElementType>
class FxStagedGPUBuffer : public FxRawGPUBuffer<ElementType>
{
private:
    using FxRawGPUBuffer<ElementType>::Create;
public:
    FxStagedGPUBuffer() = default;

    void Create(FxBufferUsageType usage, StaticArray<ElementType> &data)
    {
        this->Size = data.Size;
        Usage = usage;

        const uint64_t buffer_size = data.Size * sizeof(ElementType);

        FxRawGPUBuffer<ElementType> staging_buffer;
        staging_buffer.Create(this->Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        {
            auto context = staging_buffer.GetMappedContext();
            memcpy(context, data.Data, buffer_size);
        }

        this->Create(this->Size, EnumToInt(Usage) | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        Log::Debug(
            "thread: %lu",
            std::this_thread::get_id()
        );
        RendererVulkan->SubmitUploadCmd([&](FxCommandBuffer &cmd) {
            VkBufferCopy copy = {
                .dstOffset = 0,
                .srcOffset = 0,
                .size = buffer_size
            };
            vkCmdCopyBuffer(cmd.CommandBuffer, staging_buffer.Buffer, this->Buffer, 1, &copy);
        });
    }
public:
    FxBufferUsageType Usage;
private:
    size_t ElementSize = sizeof(ElementType);
};

}; // namespace vulkan
