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

	if (mpRecord != nullptr) {
		Destroy();
	}

	int32 status = 0;

	mpRecord = rx_buffer_create(gGraphics->GpuAllocator, static_cast<uint32>(buffer_type), size_in_bytes,
								memory_usage, static_cast<uint16>(buffer_flags), &status);

	if (mpRecord == nullptr) {
		PanicVulkan("GPUBuffer", "Error allocating GPU buffer!", static_cast<VkResult>(status));
	}

	Initialized = true;
}

void RawGpuBuffer::Map()
{
	if (IsMapped()) {
		LogWarning(LC_RENDER, "Buffer {:p} is already mapped!", reinterpret_cast<void*>(Get()));
		return;
	}

	const VkResult status = static_cast<VkResult>(rx_buffer_map(mpRecord, gGraphics->GpuAllocator));

	if (status != VK_SUCCESS) {
		LogError("Could not map GPU memory! (BufferType=0x{:x}, Error={})", static_cast<uint32>(GetType()),
				 Util::ResultToStr(status));
		return;
	}
}


void RawGpuBuffer::UnMap()
{
	if (!IsMapped()) {
		return;
	}

	rx_buffer_unmap(mpRecord, gGraphics->GpuAllocator);
}

void RawGpuBuffer::Destroy()
{
	if (!(Initialized.load()) || mpRecord == nullptr) {
		return;
	}

	Initialized.store(false);

	gAssetManager->DeleteBuffer(mpRecord);

	mpRecord = nullptr;
}

void RawGpuBuffer::Upload(const void* data, uint64 size)
{
	DebugAssert(size > 0);
	DebugAssert(data != nullptr);

	AssertMsg(GetSize() >= size, "GPU buffer is smaller than source buffer!");

	const VkResult status = static_cast<VkResult>(rx_buffer_upload(mpRecord, gGraphics->GpuAllocator, data, size));

	if (status != VK_SUCCESS) {
		LogError("Could not upload to GPU buffer! (Error={})", Util::ResultToStr(status));
	}
}

/////////////////////////////////////
// Staged Gpu Buffer Functions
/////////////////////////////////////

void GpuBuffer::Create(CommandBuffer& cmd, eGpuBufferType buffer_type, void* data, uint64 size)
{
	RawGpuBuffer staging_buffer;
	staging_buffer.Create(eGpuBufferType::Transfer, size, RX_MEMORY_CPU_TO_GPU);
	staging_buffer.Upload(data, size);

	// Create the GPU-only buffer as a transfer destination
	this->Create(buffer_type, size, RX_MEMORY_GPU_ONLY, eGpuBufferFlags::TransferReceiver);

	rx_gpu_cmd_copy_buffer(gGraphics->GetDevice()->GetRustDevice(), cmd.Get(), staging_buffer.GetRaw(), GetRaw(),
						   size);

	staging_buffer.Destroy();
}

void GpuBuffer::Create(CommandBuffer& cmd, eGpuBufferType buffer_type, const AnonArray& data)
{
	Create(cmd, buffer_type, data.pData, data.Size * data.ObjectSize);
}

} // namespace fx::renderer
