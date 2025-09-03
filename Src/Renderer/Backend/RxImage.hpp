#pragma once

#include <vulkan/vulkan.h>

// #define VMA_DEBUG_LOG(...) OldLog::Warning(__VA_ARGS__)

#include "RxCommands.hpp"
#include "RxDevice.hpp"

#include <ThirdParty/vk_mem_alloc.h>

#include <Math/FxVec2.hpp>

struct RxImageTypeProperties
{
    VkImageViewType ViewType;
    uint32 LayerCount;
};

enum class RxImageType
{
    Image2D,
    Cubemap,
};

const RxImageTypeProperties RxImageTypeGetProperties(RxImageType image_type);

class RxImage
{
public:
    RxGpuDevice* GetDevice();

    void Create(RxImageType image_type, FxVec2u size, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                VkImageAspectFlags aspect_flags);

    void Create(RxImageType image_type, FxVec2u size, VkFormat format, VkImageUsageFlags usage,
                VkImageAspectFlags aspect_flags);

    void TransitionLayout(VkImageLayout new_layout, RxCommandBuffer& cmd);

    void Destroy();

    ~RxImage() { Destroy(); }

public:
    FxVec2u Size = FxVec2u::Zero;

    VkImage Image = nullptr;
    VkImageView View = nullptr;

    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkImageLayout ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocation Allocation = nullptr;

private:
    RxGpuDevice* mDevice = nullptr;

    RxImageType mViewType = RxImageType::Image2D;
    bool mIsDepthTexture = false;
};
