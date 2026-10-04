#include "Commands.hpp"

#include "Device.hpp"

#include <Core/Assert.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("CommandBuffer")

void CommandPool::Create(GpuDevice* device, uint32 queue_family)
{
	QueueFamilyIndex = queue_family;

	mpDevice = device;

	uint64 handle = 0;
	const VkResult status = static_cast<VkResult>(
		rx_gpu_command_pool_create(mpDevice->GetRustDevice(), queue_family, &handle));

	if (status != VK_SUCCESS) {
		PanicVulkan("CommandPool", "Error creating command pool", status);
	}

	CmdPool = RxFromRaw<VkCommandPool>(handle);
}

void CommandBuffer::Create(CommandPool* pool)
{
	mpCommandPool = pool;

	mpDevice = gGraphics->GetDevice();

	void* handle = nullptr;
	const VkResult status = static_cast<VkResult>(
		rx_gpu_command_buffer_allocate(mpDevice->GetRustDevice(), RxRaw(pool->Get()), &handle));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Could not allocate command buffer", status);
	}

	Cmd = static_cast<VkCommandBuffer>(handle);

	LogDebug("Creating Command buffer 0x{:p} from queue family {:d}", reinterpret_cast<void*>(Cmd),
			 pool->QueueFamilyIndex);

	mbInitialized = true;
}

inline void CommandBuffer::CheckInitialized() const
{
	if (!IsInitialized()) {
		ModulePanic("Command buffer has not been initialized!", 0);
	}
}

void CommandBuffer::Record(VkCommandBufferUsageFlags usage_flags)
{
	CheckInitialized();

	pBoundPipeline = nullptr;

	const VkResult status = static_cast<VkResult>(rx_gpu_command_buffer_begin(mpDevice->GetRustDevice(), Cmd));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Failed to begin recording command buffer", status);
	}
}

void CommandBuffer::Reset()
{
	CheckInitialized();

	rx_gpu_command_buffer_reset(mpDevice->GetRustDevice(), Cmd);
}

void CommandBuffer::End()
{
	CheckInitialized();

	const VkResult status = static_cast<VkResult>(rx_gpu_command_buffer_end(mpDevice->GetRustDevice(), Cmd));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Failed to create command buffer!", status);
	}
}

void CommandBuffer::Destroy()
{
	rx_gpu_command_buffer_free(mpDevice->GetRustDevice(), RxRaw(mpCommandPool->Get()), Cmd);
}

} // namespace fx::renderer
