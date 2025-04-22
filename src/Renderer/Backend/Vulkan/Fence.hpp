#pragma once

#include <Core/FxPanic.hpp>
#include "Device.hpp"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace vulkan {

class Fence
{
public:
    void Create(GPUDevice *device)
    {
        const VkFenceCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            .pNext = nullptr
        };

        mDevice = device;

        const VkResult status = vkCreateFence(mDevice->Device, &create_info, nullptr, &Fence);
        if (status != VK_SUCCESS) {
            FxPanic_("Fence", "Could not create fence", status);
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
            FxPanic_("Fence", "Could not create fence", status);
        }
    }

    void Reset()
    {
        const VkResult status = vkResetFences(mDevice->Device, 1, &Fence);

        if (status != VK_SUCCESS) {
            FxPanic_("Fence", "Could not reset fence", status);
        }
    }

    void Destroy()
    {
        vkDestroyFence(mDevice->Device, Fence, nullptr);
    }

public:
    VkFence Fence;
private:
    GPUDevice *mDevice = nullptr;
};


}; // namespace vulkan
