#include "RvkCommands.hpp"
#include "RvkDevice.hpp"

#include "../Renderer.hpp"

#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("RvkCommandBuffer")

void RvkCommandBuffer::Create(RvkCommandPool *pool)
{
    mCommandPool = pool;

    const VkCommandBufferAllocateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool->CommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    mDevice = Renderer->GetDevice();

    const VkResult status = vkAllocateCommandBuffers(mDevice->Device, &buffer_info, &CommandBuffer);
    if (status != VK_SUCCESS) {
        FxModulePanic("Could not allocate command buffer", status);
    }

    Log::Debug("Creating Command buffer 0x%llx from queue family %d", CommandBuffer, pool->QueueFamilyIndex);

    mInitialized = true;
}

inline void RvkCommandBuffer::CheckInitialized() const
{
    if (!IsInitialized()) {
        FxModulePanic("Command buffer has not been initialized!", 0);
    }
}

void RvkCommandBuffer::Record(VkCommandBufferUsageFlags usage_flags)
{
    CheckInitialized();

    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr
    };

    const VkResult status = vkBeginCommandBuffer(CommandBuffer, &begin_info);
    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to begin recording command buffer", status);
    }
}

void RvkCommandBuffer::Reset()
{
    CheckInitialized();

    vkResetCommandBuffer(CommandBuffer, 0);
}

void RvkCommandBuffer::End()
{
    CheckInitialized();

    const VkResult status = vkEndCommandBuffer(CommandBuffer);
    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to create command buffer!", status);
    }
}

void RvkCommandBuffer::Destroy()
{
    vkFreeCommandBuffers(mDevice->Device, mCommandPool->CommandPool, 1, &CommandBuffer);
}
