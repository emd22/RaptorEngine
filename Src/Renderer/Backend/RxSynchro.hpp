#pragma once

#include "RxDevice.hpp"

#include <Core/FxPanic.hpp>

#include <cstdint>

#include <vulkan/vulkan.h>

class RxFence
{
public:
    void Create(RxGpuDevice *device)
    {
        const VkFenceCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            .pNext = nullptr
        };

        mDevice = device;

        const VkResult status = vkCreateFence(mDevice->Device, &create_info, nullptr, &Fence);
        if (status != VK_SUCCESS) {
            FxPanic("Fence", "Could not create fence", status);
        }
    }

    struct WaitOptions
    {
        uint64 Timeout = UINT64_MAX;
    };

    void WaitFor(WaitOptions wait_options = {.Timeout = UINT64_MAX}) const
    {
        const VkResult status = vkWaitForFences(mDevice->Device, 1, &Fence, true, wait_options.Timeout);

        if (status != VK_SUCCESS) {
            FxPanic("Fence", "Could not create fence", status);
        }
    }

    void Reset()
    {
        const VkResult status = vkResetFences(mDevice->Device, 1, &Fence);

        if (status != VK_SUCCESS) {
            FxPanic("Fence", "Could not reset fence", status);
        }
    }

    void Destroy()
    {
        vkDestroyFence(mDevice->Device, Fence, nullptr);
    }

public:
    VkFence Fence;
private:
    RxGpuDevice *mDevice = nullptr;
};


class RxSemaphore
{
public:
    void Create(RxGpuDevice *device)
    {
        const VkSemaphoreCreateInfo create_info {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .flags = 0,
            .pNext = nullptr,
        };

        mDevice = device;

        const VkResult status = vkCreateSemaphore(device->Device, &create_info, nullptr, &Semaphore);

        if (status != VK_SUCCESS) {
            FxPanic("Semaphore", "Could not create semaphore", status);
        }
    }

    void Destroy()
    {
        vkDestroySemaphore(mDevice->Device, Semaphore, nullptr);
    }

public:
    VkSemaphore Semaphore;
private:
    RxGpuDevice *mDevice = nullptr;
};
