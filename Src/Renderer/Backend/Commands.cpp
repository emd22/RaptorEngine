#include "Commands.hpp"

#include "Device.hpp"

#include <Core/Assert.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("CommandBuffer")

void CommandPool::Create(GpuDevice* device, uint32 queue_family)
{
    QueueFamilyIndex = queue_family;

    const VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family,
    };

    mpDevice = device;

    const VkResult status = vkCreateCommandPool(mpDevice->Device, &create_info, nullptr, &CmdPool);

    if (status != VK_SUCCESS) {
        PanicVulkan("CommandPool", "Error creating command pool", status);
    }
}

void CommandBuffer::Create(CommandPool* pool)
{
    mpCommandPool = pool;

    const VkCommandBufferAllocateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool->Get(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    mpDevice = gRenderer->GetDevice();

    const VkResult status = vkAllocateCommandBuffers(mpDevice->Device, &buffer_info, &Cmd);
    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Could not allocate command buffer", status);
    }

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

    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };

    const VkResult status = vkBeginCommandBuffer(Cmd, &begin_info);
    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Failed to begin recording command buffer", status);
    }
}

void CommandBuffer::Reset()
{
    CheckInitialized();

    vkResetCommandBuffer(Cmd, 0);
}

void CommandBuffer::End()
{
    CheckInitialized();

    const VkResult status = vkEndCommandBuffer(Cmd);
    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Failed to create command buffer!", status);
    }
}

void CommandBuffer::Destroy() { vkFreeCommandBuffers(mpDevice->Device, mpCommandPool->Get(), 1, &Cmd); }

} // namespace fx::renderer
