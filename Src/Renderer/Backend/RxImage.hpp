#pragma once

#include <vulkan/vulkan.h>

// #define VMA_DEBUG_LOG(...) OldLog::Warning(__VA_ARGS__)

#include "RxCommands.hpp"
#include "RxDevice.hpp"
#include "RxGpuBuffer.hpp"

#include <ThirdParty/vk_mem_alloc.h>

#include <Math/FxVec2.hpp>

struct RxImageTypeProperties
{
    VkImageViewType ViewType;
    uint32 LayerCount;
};

enum class RxImageType
{
    Image,
    Cubemap,
};

enum class RxImageAspectFlag
{
    Auto,
    Color = VK_IMAGE_ASPECT_COLOR_BIT,
    Depth = VK_IMAGE_ASPECT_DEPTH_BIT,

};

struct RxImageCubemapOptions
{
    RxImageAspectFlag AspectFlag = RxImageAspectFlag::Auto;
};

const RxImageTypeProperties RxImageTypeGetProperties(RxImageType image_type);

class RxImage
{
public:
    RxGpuDevice* GetDevice();

    void Create(RxImageType image_type, const FxVec2u& size, VkFormat format, VkImageTiling tiling,
                VkImageUsageFlags usage, VkImageAspectFlags aspect_flags);

    void Create(RxImageType image_type, const FxVec2u& size, VkFormat format, VkImageUsageFlags usage,
                VkImageAspectFlags aspect_flags);

    void TransitionLayout(VkImageLayout new_layout, RxCommandBuffer& cmd, uint32 layer_count = 1);

    void CopyFromBuffer(const RxRawGpuBuffer<uint8>& buffer, VkImageLayout final_layout, FxVec2u size,
                        uint32 base_layer = 0);

    void CreateLayeredImageFromCubemap(RxImage& cubemap, VkFormat image_format, RxImageCubemapOptions options = {});
    void Destroy();

    ~RxImage() { Destroy(); }

public:
    FxVec2u Size = FxVec2u::sZero;

    VkImage Image = nullptr;
    VkImageView View = nullptr;

    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkImageLayout ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocation Allocation = nullptr;

private:
    RxGpuDevice* mDevice = nullptr;

    RxImageType mViewType = RxImageType::Image;
    bool mIsDepthTexture = false;
};
