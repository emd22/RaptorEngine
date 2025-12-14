#pragma once

#include "RxDevice.hpp"
#include <Core/FxTypes.hpp>

#include <vulkan/vulkan.h>

class RxCommandPool
{
public:
    void Create(RxGpuDevice* device, uint32 queue_family)
    {
        QueueFamilyIndex = queue_family;

        const VkCommandPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queue_family,
        };

        mDevice = device;

        const VkResult status = vkCreateCommandPool(mDevice->Device, &create_info, nullptr, &CommandPool);

        if (status != VK_SUCCESS) {
            FxPanicVulkan("FxCommandPool", "Error creating command pool", status);
        }
    }

    void Reset() { vkResetCommandPool(mDevice->Device, CommandPool, 0); }

    void Destroy() { vkDestroyCommandPool(mDevice->Device, CommandPool, nullptr); }

public:
    VkCommandPool CommandPool = nullptr;
    uint32 QueueFamilyIndex = 0;

private:
    RxGpuDevice* mDevice = nullptr;
};

class RxCommandBuffer
{
public:
    void Create(RxCommandPool* pool);
    void Destroy();

    void Record(VkCommandBufferUsageFlags usage_flags = 0);

    void Reset();
    void End();

    operator VkCommandBuffer() const { return CommandBuffer; }

    bool IsInitialized() const { return mInitialized; }

private:
    void CheckInitialized() const;

public:
    VkCommandBuffer CommandBuffer = nullptr;

private:
    bool mInitialized = false;
    RxCommandPool* mCommandPool = nullptr;
    RxGpuDevice* mDevice = nullptr;
};
