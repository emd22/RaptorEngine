#include "GpuBuffer.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {


class BufferTracker
{
private:
    struct Entry
    {
        uint32 Id;
        bool bExists = false;
        eGpuBufferType BufferType;
        uint64 Size;
        eGpuBufferFlags Flags;
    };

public:
    void AddBuffer(uint32 id, eGpuBufferType type, uint64 size, eGpuBufferFlags flags)
    {
        Entries.emplace_back(id, true, type, size, flags);
    }

    void RemoveBuffer(uint32 id) { Entries[id].bExists = false; }

    void PrintUndestroyed()
    {
        for (const Entry& entry : Entries) {
            if (!entry.bExists) {
                continue;
            }

            LogWarning(LC_RENDER, "[NOT DESTROYED]: Type={}, Size={}, Persistent?={}, TransferReciever?={}",
                       GpuBufferUtil::BufferTypeToName(entry.BufferType), entry.Size,
                       (entry.Flags & eGpuBufferFlags::PersistentMapped) != 0,
                       (entry.Flags & eGpuBufferFlags::TransferReceiver) != 0);
        }
    }

    std::vector<Entry> Entries;
};

static BufferTracker gBufferTracker;


void GpuBufferPrintUndestroyed() { gBufferTracker.PrintUndestroyed(); }


void RawGpuBuffer::Create(eGpuBufferType buffer_type, uint64 size_in_bytes, VmaMemoryUsage memory_usage,
                          eGpuBufferFlags buffer_flags)
{
    Assert(size_in_bytes > 0);

    static uint32 CurrentId = 0;

    BufferId = CurrentId++;

    Size = size_in_bytes;
    Type = buffer_type;
    mBufferFlags = buffer_flags;

    gBufferTracker.AddBuffer(BufferId, Type, Size, mBufferFlags);

    if (BufferId == 6) {
        FX_BREAKPOINT;
    }

    // LogInfo("[Created GPU Buffer]: Type={}, Size={}, Persistent?={}, TransferReciever?={}",
    // GpuBufferUtil::BufferTypeToName(buffer_type), size_in_bytes, (buffer_flags & eGpuBufferFlags::PersistentMapped)
    // != 0, (buffer_flags & eGpuBufferFlags::TransferReceiver) != 0);

    VmaAllocationCreateFlags vma_create_flags = 0;

    if ((mBufferFlags & eGpuBufferFlags::PersistentMapped) != 0) {
        vma_create_flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    VkBufferUsageFlags usage_flags = GpuBufferUtil::BufferTypeToUnderlying(buffer_type);

    if ((buffer_flags & eGpuBufferFlags::TransferReceiver) != 0) {
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
    const VkResult status = vmaCreateBuffer(gRenderer->GpuAllocator, &create_info, &alloc_create_info, &Buffer,
                                            &Allocation, &allocation_info);

    if (status != VK_SUCCESS) {
        PanicVulkan("GPUBuffer", "Error allocating GPU buffer!", status);
    }


    // LogInfo("Create Buffer  (Buffer={:p}, Allocation={:p}, Size={})", reinterpret_cast<void*>(Buffer),
    //           reinterpret_cast<void*>(Allocation), Size);


    if ((mBufferFlags & eGpuBufferFlags::PersistentMapped) != 0) {
        // Get the pointer from VMA for the mapped GPU buffer
        pMappedBuffer = allocation_info.pMappedData;
    }

    Initialized = true;
}

void RawGpuBuffer::Map()
{
    if (IsMapped()) {
        LogWarning(LC_RENDER, "Buffer {:p} is already mapped!", reinterpret_cast<void*>(Buffer));
        return;
    }

    const VkResult status = vmaMapMemory(gRenderer->GpuAllocator, Allocation, &pMappedBuffer);

    if (status != VK_SUCCESS) {
        LogError("Could not map GPU memory! (BufferType=0x{:x}, Error={})", static_cast<uint32>(Type),
                 Util::ResultToStr(status));
        return;
    }
}


void RawGpuBuffer::UnMap()
{
    if (!IsMapped()) {
        return;
    }

    vmaUnmapMemory(gRenderer->GpuAllocator, Allocation);
    pMappedBuffer = nullptr;
}

void RawGpuBuffer::Destroy()
{
    if (!(Initialized.load()) || Allocation == nullptr || Buffer == nullptr) {
        return;
    }

    gBufferTracker.RemoveBuffer(BufferId);

    // LogInfo("[Destroyed GPU Buffer]: Type={}, Size={}, Persistent?={}, TransferReciever?={}",
    // GpuBufferUtil::BufferTypeToName(Type), Size, (mBufferFlags & eGpuBufferFlags::PersistentMapped) != 0,
    // (mBufferFlags & eGpuBufferFlags::TransferReceiver) != 0);

    gRenderer->AddGpuBufferToDeletionQueue(Buffer, Allocation);

    Initialized.store(false);
    Size = 0;

    Allocation = nullptr;
    Buffer = nullptr;
}

void RawGpuBuffer::Upload(void* data, uint64 size)
{
    DebugAssert(size > 0);
    DebugAssert(data != nullptr);

    AssertMsg(this->Size >= size, "GPU buffer is smaller than source buffer!");

    Map();
    memcpy(pMappedBuffer, data, size);
    UnMap();
}

/////////////////////////////////////
// Staged Gpu Buffer Functions
/////////////////////////////////////

void GpuBuffer::Create(CommandBuffer& cmd, eGpuBufferType buffer_type, void* data, uint64 size)
{
    Size = size;
    Type = buffer_type;

    RawGpuBuffer staging_buffer;
    staging_buffer.Create(eGpuBufferType::Transfer, Size, VMA_MEMORY_USAGE_CPU_TO_GPU);
    staging_buffer.Upload(data, size);

    // Create the GPU-only buffer as a transfer destination
    this->Create(buffer_type, this->Size, VMA_MEMORY_USAGE_GPU_ONLY, eGpuBufferFlags::TransferReceiver);

    VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = Size };
    vkCmdCopyBuffer(cmd.Get(), staging_buffer.Buffer, this->Buffer, 1, &copy);

    staging_buffer.Destroy();
}

void GpuBuffer::Create(CommandBuffer& cmd, eGpuBufferType buffer_type, const AnonArray& data)
{
    Create(cmd, buffer_type, data.pData, data.Size * data.ObjectSize);
}

} // namespace fx::renderer
