#pragma once

#include <vulkan/vulkan.h>

#include "RvkDevice.hpp"

namespace vulkan {


class RvkCommandPool
{
public:

    void Create(RvkGpuDevice *device, uint32 queue_family)
    {
        QueueFamilyIndex = queue_family;

        const VkCommandPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = queue_family,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        };

        mDevice = device;

        const VkResult status = vkCreateCommandPool(mDevice->Device, &create_info, nullptr, &CommandPool);

        if (status != VK_SUCCESS) {
            FxPanic("FxCommandPool", "Error creating command pool", 0);
        }
    }

    void Reset()
    {
        vkResetCommandPool(mDevice->Device, CommandPool, 0);
    }

    void Destroy()
    {
        vkDestroyCommandPool(mDevice->Device, CommandPool, nullptr);
    }

public:
    VkCommandPool CommandPool = nullptr;
    uint32 QueueFamilyIndex = 0;
private:
    RvkGpuDevice *mDevice = nullptr;
};

class RvkCommandBuffer
{
public:
    void Create(RvkCommandPool *pool);
    void Destroy();

    void Record(VkCommandBufferUsageFlags usage_flags = 0);

    void Reset();
    void End();

    operator VkCommandBuffer() const { return CommandBuffer; }

    bool IsInitialized() const
    {
        return mInitialized;
    }

private:
    void CheckInitialized() const;

public:
    VkCommandBuffer CommandBuffer = nullptr;
private:
    bool mInitialized = false;
    RvkCommandPool *mCommandPool = nullptr;
    RvkGpuDevice *mDevice = nullptr;
};

}; // namespace vulkan
