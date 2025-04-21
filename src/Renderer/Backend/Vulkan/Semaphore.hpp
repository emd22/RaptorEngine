#pragma once

#include <Core/FxPanic.hpp>
#include "Device.hpp"
#include "vulkan/vulkan_core.h"
#include <vulkan/vulkan.h>

namespace vulkan {

class Semaphore
{
public:
    void Create(GPUDevice *device)
    {
        const VkSemaphoreCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .flags = 0,
            .pNext = nullptr,
        };

        mDevice = device;

        const VkResult status = vkCreateSemaphore(device->Device, &create_info, nullptr, &Semaphore);

        if (status != VK_SUCCESS) {
            FxPanic_("Semaphore", "Could not create semaphore", status);
        }
    }

    void Destroy()
    {
        vkDestroySemaphore(mDevice->Device, Semaphore, nullptr);
    }

public:
    VkSemaphore Semaphore;
private:
    GPUDevice *mDevice = nullptr;
};

};
