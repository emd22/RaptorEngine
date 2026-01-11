#pragma once

#include <vulkan/vulkan.h>

// #define VMA_DEBUG_LOG(...) OldLog::Warning(__VA_ARGS__)

#include "RxCommands.hpp"
#include "RxDevice.hpp"
#include "RxGpuBuffer.hpp"

#include <ThirdParty/vk_mem_alloc.h>

#include <Math/FxVec2.hpp>
#include <optional>

enum class RxImageFormat
{
    eNone,

    // Color formats

    eRG32_Float,
    eBGRA8_UNorm,
    eRGBA8_SRGB,
    eRGBA8_UNorm,
    eRGBA16_Float,
    eRGB32_Float,

    /// DO NOT USE FORMAT: Marker for depth formats
    _eDepthFormatsBegin,

    eD16_UNorm_S8_SInt,
    eD32_Float_S8_SInt,

    /// DO NOT USE FORMAT: Marker for depth formats
    _eDepthFormatsEnd,
};


struct RxImageFormatUtil
{
    static constexpr bool IsDepth(RxImageFormat format)
    {
        if (format > RxImageFormat::_eDepthFormatsBegin && format < RxImageFormat::_eDepthFormatsEnd) {
            return true;
        }

        return false;
    }


    static constexpr VkFormat ToUnderlying(RxImageFormat format)
    {
        switch (format) {
        case RxImageFormat::eNone:
            break;

            // Color formats

        case RxImageFormat::eRG32_Float:
            return VK_FORMAT_R32G32_SFLOAT;
        case RxImageFormat::eBGRA8_UNorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case RxImageFormat::eRGBA8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case RxImageFormat::eRGBA8_UNorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case RxImageFormat::eRGBA16_Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RxImageFormat::eRGB32_Float:
            return VK_FORMAT_R32G32B32_SFLOAT;

            // Depth Formats

        case RxImageFormat::eD16_UNorm_S8_SInt:
            return VK_FORMAT_D16_UNORM_S8_UINT;
        case RxImageFormat::eD32_Float_S8_SInt:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;

        // Markers
        case RxImageFormat::_eDepthFormatsBegin:
        case RxImageFormat::_eDepthFormatsEnd:
            break;
        }

        return VK_FORMAT_UNDEFINED;
    }
};


struct RxImageTypeProperties
{
    VkImageViewType ViewType;
    uint32 LayerCount;
};

enum class RxImageType
{
    e2d,
    eCubemap,
};

enum class RxImageAspectFlag
{
    eAuto,
    eColor = VK_IMAGE_ASPECT_COLOR_BIT,
    eDepth = VK_IMAGE_ASPECT_DEPTH_BIT,

};

struct RxTransitionLayoutOverrides
{
    std::optional<VkPipelineStageFlagBits> DstStage = std::nullopt;
    std::optional<VkAccessFlags> DstAccessMask = std::nullopt;
};

struct RxImageCubemapOptions
{
    RxImageAspectFlag AspectFlag = RxImageAspectFlag::eAuto;
};

const RxImageTypeProperties RxImageTypeGetProperties(RxImageType image_type);

class RxImage
{
public:
    void Create(RxImageType image_type, const FxVec2u& size, RxImageFormat format, VkImageTiling tiling,
                VkImageUsageFlags usage, VkImageAspectFlags aspect_flags);

    void Create(RxImageType image_type, const FxVec2u& size, RxImageFormat format, VkImageUsageFlags usage,
                VkImageAspectFlags aspect_flags);

    void TransitionLayout(VkImageLayout new_layout, RxCommandBuffer& cmd, uint32 layer_count = 1,
                          std::optional<RxTransitionLayoutOverrides> overrides = std::nullopt);

    void CopyFromBuffer(const RxRawGpuBuffer<uint8>& buffer, VkImageLayout final_layout, FxVec2u size,
                        uint32 base_layer = 0);

    void CreateLayeredImageFromCubemap(RxImage& cubemap, RxImageFormat image_format, VkImageAspectFlags aspect_flags,
                                       RxImageCubemapOptions options);

    void Destroy();

    ~RxImage() { Destroy(); }

public:
    FxVec2u Size = FxVec2u::sZero;

    VkImage Image = nullptr;
    VkImageView View = nullptr;

    RxImageType ViewType = RxImageType::e2d;
    RxImageFormat Format = RxImageFormat::eNone;
    VkImageLayout ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocation Allocation = nullptr;
};
