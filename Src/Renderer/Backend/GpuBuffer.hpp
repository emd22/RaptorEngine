#pragma once

#include "Fwd/Fwd_AddToDeletionQueue.hpp"
#include "Fwd/Fwd_GetGpuAllocator.hpp"
#include "Fwd/Fwd_SubmitUploadGpuCmd.hpp"

#include <Core/AnonArray.hpp>
#include <Core/Slice.hpp>
#include <Core/Types.hpp>
#include <Core/Util.hpp>

namespace fx {

enum class eGpuBufferFlags : uint16
{
    None = 0,
    /** The buffer is mapped for the lifetime of the buffer. */
    PersistentMapped = (1 << 0),
    TransferReceiver = (1 << 1),
};
FxEnumFlags(eGpuBufferFlags);

namespace renderer {

// #define FX_DEBUG_GPU_BUFFER_ALLOCATION_NAMES 1

class RawGpuBuffer;
class GpuBuffer;

enum class eBufferUsageType
{
    Vertices = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    Indices = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
};


// FX_DEFINE_ENUM_AS_FLAGS(GpuBufferFlags);

enum class eGpuBufferType
{
    Storage,
    StorageWithOffset,
    Uniform,
    UniformWithOffset,
    Transfer,
    VertexBuffer,
    IndexBuffer,
};


namespace GpuBufferUtil {
static constexpr VkBufferUsageFlags BufferTypeToUnderlying(eGpuBufferType type)
{
    switch (type) {
    case eGpuBufferType::Storage:
    case eGpuBufferType::StorageWithOffset:
        return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    case eGpuBufferType::Uniform:
    case eGpuBufferType::UniformWithOffset:
        return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    case eGpuBufferType::Transfer:
        return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    case eGpuBufferType::VertexBuffer:
        return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case eGpuBufferType::IndexBuffer:
        return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }

    return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
}

#define ENUM_TYPE eGpuBufferType

static constexpr const char* BufferTypeToName(const eGpuBufferType type)
{
    switch (type) {
        FX_ENUM_CASE_NAME(Storage);
        FX_ENUM_CASE_NAME(StorageWithOffset);
        FX_ENUM_CASE_NAME(Uniform);
        FX_ENUM_CASE_NAME(UniformWithOffset);
        FX_ENUM_CASE_NAME(Transfer);
        FX_ENUM_CASE_NAME(VertexBuffer);
        FX_ENUM_CASE_NAME(IndexBuffer);
    }

    return "";
}

#undef ENUM_TYPE


static constexpr VkDescriptorType BufferTypeToDescriptorType(eGpuBufferType type)
{
    switch (type) {
    case eGpuBufferType::Storage:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    case eGpuBufferType::StorageWithOffset:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

    case eGpuBufferType::Uniform:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    case eGpuBufferType::UniformWithOffset:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

    default:;
    }

    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}
}; // namespace GpuBufferUtil

void GpuBufferPrintUndestroyed();

/**
 * @brief Provides a GPU buffer that can be created with more complex parameters without staging.
 */
class RawGpuBuffer
{
public:
    RawGpuBuffer() = default;

    RawGpuBuffer(RawGpuBuffer& other) = delete;

    RawGpuBuffer operator=(RawGpuBuffer& other) = delete;

    void Create(eGpuBufferType buffer_type, uint64 size_in_bytes, VmaMemoryUsage memory_usage,
                eGpuBufferFlags buffer_flags = eGpuBufferFlags::None);

    void FlushToGpu(uint32 offset, uint32 size)
    {
        vmaFlushAllocation(Fx_Fwd_GetGpuAllocator(), Allocation, offset, size);
    }

    void Map();
    void UnMap();

    bool IsMapped() const { return pMappedBuffer != nullptr; }

    /**
     * @brief Uploads raw data buffer to the GPU buffer.
     * @param data The data to upload
     * @param size The size of the buffer in bytes
     */
    void Upload(void* data, uint64 size);

    template <typename TElementType>
    void Upload(const SizedArray<TElementType>& data)
    {
        Upload(reinterpret_cast<void*>(data.pData), data.GetSizeInBytes());
    }

    void Destroy();

    ~RawGpuBuffer() { Destroy(); }

public:
    eGpuBufferType Type = eGpuBufferType::Storage;
    uint32 BufferId = 0;

    VkBuffer Buffer = nullptr;
    VmaAllocation Allocation = nullptr;

    void* pMappedBuffer = nullptr;

    std::atomic_bool Initialized = false;
    uint64 Size = 0;

private:
    eGpuBufferFlags mBufferFlags = eGpuBufferFlags::None;
};


class GpuBufferMapContext
{
public:
    GpuBufferMapContext(RawGpuBuffer* buffer) : mpGpuBuffer(buffer) { mpGpuBuffer->Map(); }

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
        DebugAssert(mpGpuBuffer->pMappedBuffer != nullptr);
        return static_cast<TElementType*>(mpGpuBuffer->pMappedBuffer);
    }

    ~GpuBufferMapContext() { mpGpuBuffer->UnMap(); }

    /**
     * Manually unmaps the buffer.
     */
    void UnMap() const { mpGpuBuffer->UnMap(); }

private:
    RawGpuBuffer* mpGpuBuffer = nullptr;
};


/**
 * @brief A GPU buffer that is created CPU side, and copied over to a GPU-only buffer. This is the default
 * buffer type.
 */
class GpuBuffer : public RawGpuBuffer
{
private:
    using RawGpuBuffer::Create;

public:
    GpuBuffer() = default;


    void Create(eGpuBufferType buffer_type, void* data, uint64 size);
    void Create(eGpuBufferType buffer_type, const AnonArray& data);

    template <typename TElementType>
    void Create(eGpuBufferType buffer_type, const Slice<TElementType>& data)
    {
        Size = data.Size * sizeof(TElementType);
        Type = buffer_type;

        RawGpuBuffer staging_buffer;
        staging_buffer.Create(eGpuBufferType::Transfer, Size, VMA_MEMORY_USAGE_CPU_TO_GPU);
        staging_buffer.Upload(data, Size);

        // Create the GPU-only buffer as a transfer destination
        this->Create(buffer_type, this->Size, VMA_MEMORY_USAGE_GPU_ONLY, eGpuBufferFlags::TransferReceiver);

        // Transfer
        Fx_Fwd_SubmitUploadCmd(
            [&](CommandBuffer& cmd)
            {
                VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = Size };
                vkCmdCopyBuffer(cmd.Get(), staging_buffer.Buffer, this->Buffer, 1, &copy);
            });

        staging_buffer.Destroy();
    }
};

} // namespace renderer

} // namespace fx
