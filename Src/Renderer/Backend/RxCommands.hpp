#pragma once

#include "RxDevice.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxTypes.hpp>

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

        mpDevice = device;

        const VkResult status = vkCreateCommandPool(mpDevice->Device, &create_info, nullptr, &CommandPool);

        if (status != VK_SUCCESS) {
            FxPanicVulkan("FxCommandPool", "Error creating command pool", status);
        }
    }

    void Reset() { vkResetCommandPool(mpDevice->Device, CommandPool, 0); }

    void Destroy() { vkDestroyCommandPool(mpDevice->Device, CommandPool, nullptr); }

public:
    VkCommandPool CommandPool = nullptr;

    uint32 QueueFamilyIndex = 0;

private:
    RxGpuDevice* mpDevice = nullptr;
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

    bool IsInitialized() const { return mbInitialized; }

private:
    void CheckInitialized() const;

public:
    VkCommandBuffer CommandBuffer = nullptr;

private:
    bool mbInitialized = false;
    RxCommandPool* mpCommandPool = nullptr;
    RxGpuDevice* mpDevice = nullptr;
};
