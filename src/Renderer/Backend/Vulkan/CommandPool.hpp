#pragma once

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>
#include <Core/FxPanic.hpp>

#include "Device.hpp"

namespace vulkan {

class CommandPool
{
public:

    void Create(GPUDevice *device, uint32 queue_family)
    {
        const VkCommandPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = queue_family,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        };

        mDevice = device;

        const VkResult status = vkCreateCommandPool(mDevice->Device, &create_info, nullptr, &CommandPool);

        if (status != VK_SUCCESS) {
            FxPanic_("CommandPool", "Error creating command pool", 0);
        }
    }

    void Destroy()
    {
        vkDestroyCommandPool(mDevice->Device, CommandPool, nullptr);
    }

public:
    VkCommandPool CommandPool = nullptr;
private:
    GPUDevice *mDevice = nullptr;
};


}; // namespace vulkan
