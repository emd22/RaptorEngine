#pragma once

#include <vulkan/vulkan.h>

#define VMA_DEBUG_LOG(...) Log::Warning(__VA_ARGS__)

#include <ThirdParty/vk_mem_alloc.h>


#include "RvkDevice.hpp"
#include "RvkCommands.hpp"

#include <Math/Vec2.hpp>

class RvkImage
{
public:
    void Create(
        Vec2u size,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkImageAspectFlagBits aspect_flags
    );

    void TransitionLayout(VkImageLayout new_layout, RvkCommandBuffer &cmd);

    void Destroy();

    ~RvkImage()
    {
        Destroy();
    }

public:
    Vec2u Size = Vec2u::Zero;

    VkImage Image = nullptr;
    VkImageView View = nullptr;

    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkImageLayout ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocation Allocation = nullptr;

private:
    RvkGpuDevice *mDevice = nullptr;
};
