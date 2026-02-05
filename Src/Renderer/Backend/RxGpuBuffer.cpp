#include "RxGpuBuffer.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>

void RxGpuBufferSubmitUploadCmd(std::function<void(RxCommandBuffer&)> func) { gRenderer->SubmitUploadCmd(func); }

void RxRawGpuBuffer::Create(RxGpuBufferType buffer_type, uint64 size_in_bytes, VmaMemoryUsage memory_usage,
                            RxGpuBufferFlags buffer_flags)
{
    Size = size_in_bytes;
    Type = buffer_type;
    mBufferFlags = buffer_flags;

    VmaAllocationCreateFlags vma_create_flags = 0;

    if ((mBufferFlags & RxGpuBufferFlags::ePersistentMapped)) {
        vma_create_flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    VkBufferUsageFlags usage_flags = RxGpuBufferUtil::BufferTypeToUnderlying(buffer_type);

    if (buffer_flags & RxGpuBufferFlags::eTransferReceiver) {
        usage_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    const VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .flags = 0,
        .size = size_in_bytes,
        .usage = usage_flags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

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

void RxRawGpuBuffer::Map()
{
    if (IsMapped()) {
        FxLogWarning("Buffer {:p} is already mapped!", reinterpret_cast<void*>(Buffer));
        return;
    }

    const VkResult status = vmaMapMemory(Fx_Fwd_GetGpuAllocator(), Allocation, &pMappedBuffer);

    if (status != VK_SUCCESS) {
        FxLogError("Could not map GPU memory! (BufferType=0x{:x}, Error={})", static_cast<uint32>(Type),
                   RxUtil::ResultToStr(status));
        return;
    }
}


void RxRawGpuBuffer::UnMap()
{
    if (!IsMapped()) {
        return;
    }

    vmaUnmapMemory(Fx_Fwd_GetGpuAllocator(), Allocation);
    pMappedBuffer = nullptr;
}

void RxRawGpuBuffer::Destroy()
{
    if (!(Initialized.load()) || Allocation == nullptr || Buffer == nullptr) {
        return;
    }

    Fx_Fwd_AddGpuBufferToDeletionQueue(this->Buffer, this->Allocation);

    Initialized.store(false);
    Size = 0;
}
