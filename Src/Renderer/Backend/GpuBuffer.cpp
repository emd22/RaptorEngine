#include "GpuBuffer.hpp"

#include <Asset/AssetManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

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


void RawGpuBuffer::Create(eGpuBufferType buffer_type, uint64 size_in_bytes, RxMemoryUsage memory_usage,
						  eGpuBufferFlags buffer_flags)
{
	Assert(size_in_bytes > 0);

	Assert(buffer_type != eGpuBufferType::None);

	static uint32 CurrentId = 0;

	BufferId = CurrentId++;

	Size = size_in_bytes;
	Type = buffer_type;
	mBufferFlags = buffer_flags;

	// gBufferTracker.AddBuffer(BufferId, Type, Size, mBufferFlags);

	// LogInfo("[Created GPU Buffer]: Type={}, Size={}, Persistent?={}, TransferReciever?={}",
	// GpuBufferUtil::BufferTypeToName(buffer_type), size_in_bytes, (buffer_flags & eGpuBufferFlags::PersistentMapped)
	// != 0, (buffer_flags & eGpuBufferFlags::TransferReceiver) != 0);

	uint64 buffer_handle = 0;
	void* mapped_data = nullptr;

	const VkResult status = static_cast<VkResult>(
		rx_gpu_buffer_create_typed(gGraphics->GpuAllocator, static_cast<uint32>(buffer_type), size_in_bytes,
								   memory_usage, static_cast<uint16>(buffer_flags), &buffer_handle, &Allocation,
								   &mapped_data));

	if (status != VK_SUCCESS) {
		PanicVulkan("GPUBuffer", "Error allocating GPU buffer!", status);
	}

	Buffer = RxFromRaw<VkBuffer>(buffer_handle);

	if (reinterpret_cast<uintptr_t>(Buffer) == 0x850000000085) {
		// FX_BREAKPOINT;
	}

	// LogInfo("Create Buffer  (Buffer={:p}, Allocation={:p}, Size={})", reinterpret_cast<void*>(Buffer),
	//           reinterpret_cast<void*>(Allocation), Size);


	if ((mBufferFlags & eGpuBufferFlags::PersistentMapped) != 0) {
		// Get the pointer from VMA for the mapped GPU buffer
		pMappedBuffer = mapped_data;
	}

	Initialized = true;
}

void RawGpuBuffer::Map()
{
	if (IsMapped()) {
		LogWarning(LC_RENDER, "Buffer {:p} is already mapped!", reinterpret_cast<void*>(Buffer));
		return;
	}

	const VkResult status = static_cast<VkResult>(
		rx_gpu_allocation_map(gGraphics->GpuAllocator, Allocation, &pMappedBuffer));

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

	rx_gpu_allocation_unmap(gGraphics->GpuAllocator, Allocation);
	pMappedBuffer = nullptr;
}

void RawGpuBuffer::Destroy()
{
	if (!(Initialized.load()) || Allocation == nullptr || Buffer == nullptr) {
		return;
	}

	// gBufferTracker.RemoveBuffer(BufferId);

	// LogInfo("[Destroyed GPU Buffer]: Type={}, Size={}, Persistent?={}, TransferReciever?={}",
	// GpuBufferUtil::BufferTypeToName(Type), Size, (mBufferFlags & eGpuBufferFlags::PersistentMapped) != 0,
	// (mBufferFlags & eGpuBufferFlags::TransferReceiver) != 0);

	// gRenderer->AddGpuBufferToDeletionQueue(Buffer, Allocation);

	gAssetManager->DeleteBuffer(*this);

	Initialized.store(false);
	Size = 0;

	Allocation = nullptr;
	Buffer = nullptr;
}

void RawGpuBuffer::Upload(const void* data, uint64 size)
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
	staging_buffer.Create(eGpuBufferType::Transfer, Size, RX_MEMORY_CPU_TO_GPU);
	staging_buffer.Upload(data, size);

	// Create the GPU-only buffer as a transfer destination
	this->Create(buffer_type, this->Size, RX_MEMORY_GPU_ONLY, eGpuBufferFlags::TransferReceiver);

	rx_gpu_cmd_copy_buffer(gGraphics->GetDevice()->GetRustDevice(), cmd.Get(), RxRaw(staging_buffer.Buffer),
						   RxRaw(this->Buffer), Size);

	staging_buffer.Destroy();
}

void GpuBuffer::Create(CommandBuffer& cmd, eGpuBufferType buffer_type, const AnonArray& data)
{
	Create(cmd, buffer_type, data.pData, data.Size * data.ObjectSize);
}

} // namespace fx::renderer
