#pragma once

#include "Device.hpp"
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
	None,
	Storage,
	StorageWithOffset,
	Uniform,
	UniformWithOffset,
	Transfer,
	VertexBuffer,
	IndexBuffer,
};


namespace GpuBufferUtil {

static_assert(static_cast<uint32>(eGpuBufferType::None) == RX_BUFFER_NONE);
static_assert(static_cast<uint32>(eGpuBufferType::Storage) == RX_BUFFER_STORAGE);
static_assert(static_cast<uint32>(eGpuBufferType::StorageWithOffset) == RX_BUFFER_STORAGE_WITH_OFFSET);
static_assert(static_cast<uint32>(eGpuBufferType::Uniform) == RX_BUFFER_UNIFORM);
static_assert(static_cast<uint32>(eGpuBufferType::UniformWithOffset) == RX_BUFFER_UNIFORM_WITH_OFFSET);
static_assert(static_cast<uint32>(eGpuBufferType::Transfer) == RX_BUFFER_TRANSFER);
static_assert(static_cast<uint32>(eGpuBufferType::VertexBuffer) == RX_BUFFER_VERTEX);
static_assert(static_cast<uint32>(eGpuBufferType::IndexBuffer) == RX_BUFFER_INDEX);

static_assert(static_cast<uint16>(eGpuBufferFlags::PersistentMapped) == RX_BUFFER_FLAG_PERSISTENT_MAPPED);
static_assert(static_cast<uint16>(eGpuBufferFlags::TransferReceiver) == RX_BUFFER_FLAG_TRANSFER_RECEIVER);

inline VkBufferUsageFlags BufferTypeToUnderlying(eGpuBufferType type)
{
	return static_cast<VkBufferUsageFlags>(rx_buffer_type_usage(static_cast<uint32>(type)));
}

inline const char* BufferTypeToName(const eGpuBufferType type)
{
	return rx_buffer_type_name(static_cast<uint32>(type));
}

inline VkDescriptorType BufferTypeToDescriptorType(eGpuBufferType type)
{
	return static_cast<VkDescriptorType>(rx_buffer_type_descriptor_type(static_cast<uint32>(type)));
}

}; // namespace GpuBufferUtil

void GpuBufferPrintUndestroyed();

/**
 * @brief Provides a GPU buffer that can be created with more complex parameters without staging.
 */
class RawGpuBuffer
{
public:
	RawGpuBuffer() { mpRecord = rx_buffer_new(); }

	RawGpuBuffer(const RawGpuBuffer& other) = delete;

	RawGpuBuffer& operator=(const RawGpuBuffer& other) = delete;

	void Create(eGpuBufferType buffer_type, uint64 size_in_bytes, RxMemoryUsage memory_usage,
				eGpuBufferFlags buffer_flags = eGpuBufferFlags::None);

	void FlushToGpu(uint32 offset, uint32 size)
	{
		rx_buffer_flush(mpRecord, Fx_Fwd_GetGpuAllocator(), offset, size);
	}

	/// Invalidates host caches so CPU reads see GPU writes (e.g. image-to-buffer
	/// readbacks into GPU_TO_CPU staging buffers). Must be called after Map()
	/// and before reading the mapped memory. No-op on coherent memory.
	/// This is essentially the CPU equivalent of `FlushToGpu`.
	void InvalidateFromGpu()
	{
		rx_buffer_invalidate(mpRecord, Fx_Fwd_GetGpuAllocator());
	}

	void Map();
	void UnMap();

	bool IsMapped() const { return GetMapped() != nullptr; }

	FX_FORCE_INLINE VkBuffer Get() const { return reinterpret_cast<VkBuffer>(mpRecord->buffer); }

	FX_FORCE_INLINE uint64 GetRaw() const { return mpRecord->buffer; }
	FX_FORCE_INLINE uint64 GetSize() const { return mpRecord->size; }

	FX_FORCE_INLINE eGpuBufferType GetType() const { return static_cast<eGpuBufferType>(mpRecord->buffer_type); }

	/// The mapped memory, if the buffer is mapped.
	FX_FORCE_INLINE void* GetMapped() const { return mpRecord->mapped; }

	/// The slot the buffer lives in. It stays the same across `Destroy()` and `Create()`, so things that hold onto it
	/// follow the buffer.
	FX_FORCE_INLINE RxBuffer* GetRecord() const { return mpRecord; }

	/**
	 * @brief Uploads raw data buffer to the GPU buffer.
	 * @param data The data to upload
	 * @param size The size of the buffer in bytes
	 */
	void Upload(const void* data, uint64 size);

	template <typename TElementType>
	void Upload(const Slice<TElementType>& data)
	{
		Upload(reinterpret_cast<const void*>(data.pData), data.GetSizeInBytes());
	}

	void Destroy();

	~RawGpuBuffer();

public:
	std::atomic_bool Initialized = { false };

private:
	/// The buffer's state and its memory live in a slot owned by Rust.
	RxBuffer* mpRecord = nullptr;
};


class GpuBufferMapContext
{
public:
	GpuBufferMapContext(RawGpuBuffer* buffer) : mpGpuBuffer(buffer) { mpGpuBuffer->Map(); }

	/** Returns the raw pointer representation of the mapped data. */
	operator void*()
	{
		return mpGpuBuffer->GetMapped();
	}

	/** Returns the pointer representation of the mapped data. */
	template <typename TElementType>
	TElementType* GetPtr()
	{
		DebugAssert(mpGpuBuffer->GetMapped() != nullptr);
		return static_cast<TElementType*>(mpGpuBuffer->GetMapped());
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


	void Create(CommandBuffer& cmd, eGpuBufferType buffer_type, void* data, uint64 size);
	void Create(CommandBuffer& cmd, eGpuBufferType buffer_type, const AnonArray& data);

	template <typename TElementType>
	void Create(CommandBuffer& cmd, eGpuBufferType buffer_type, const Slice<TElementType>& data)
	{
		Create(cmd, buffer_type, data.pData, data.Size * sizeof(TElementType));
		// Size = data.Size * sizeof(TElementType);
		// Type = buffer_type;

		// pStagingBuffer = gEnginePool->Alloc<RawGpuBuffer>(sizeof(RawGpuBuffer));
		// pStagingBuffer->Create(eGpuBufferType::Transfer, Size, RX_MEMORY_CPU_TO_GPU);
		// pStagingBuffer->Upload(data, Size);

		// // Create the GPU-only buffer as a transfer destination
		// this->Create(buffer_type, this->Size, RX_MEMORY_GPU_ONLY, eGpuBufferFlags::TransferReceiver);

		// VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = Size };
		// vkCmdCopyBuffer(cmd.Get(), pStagingBuffer->Buffer, this->Buffer, 1, &copy);

		// // staging_buffer.Destroy();
	}

public:
	RawGpuBuffer* pStagingBuffer = nullptr;
};

} // namespace renderer

} // namespace fx


template <>
struct std::formatter<fx::renderer::eGpuBufferType>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(fx::renderer::eGpuBufferType type, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "{}", fx::renderer::GpuBufferUtil::BufferTypeToName(type));
	}
};
