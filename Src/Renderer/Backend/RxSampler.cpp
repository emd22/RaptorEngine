#include "RxSampler.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>


static FX_FORCE_INLINE VkSamplerMipmapMode GetVkMipMode(RxSamplerFilter filter)
{
    switch (filter) {
    case RxSamplerFilter::eNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case RxSamplerFilter::eLinear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

static FX_FORCE_INLINE VkFilter GetVkFilter(RxSamplerFilter filter)
{
    switch (filter) {
    case RxSamplerFilter::eNearest:
        return VK_FILTER_NEAREST;
    case RxSamplerFilter::eLinear:
        return VK_FILTER_LINEAR;
    }

    return VK_FILTER_LINEAR;
}

static FX_FORCE_INLINE VkBorderColor GetVkBorderColor(RxSamplerBorderColor color)
{
    switch (color) {
    case RxSamplerBorderColor::eIntBlack:
        return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case RxSamplerBorderColor::eFloatBlack:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    case RxSamplerBorderColor::eIntWhite:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    case RxSamplerBorderColor::eFloatWhite:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;

    case RxSamplerBorderColor::eIntTransparent:
        return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case RxSamplerBorderColor::eFloatTransparent:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}

static FX_FORCE_INLINE VkSamplerAddressMode GetVkAddressMode(RxSamplerAddressMode addr_mode)
{
    switch (addr_mode) {
    case RxSamplerAddressMode::eRepeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RxSamplerAddressMode::eClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
}

static FX_FORCE_INLINE VkCompareOp GetVkCompareOp(RxSamplerCompareOp op)
{
    switch (op) {
    case RxSamplerCompareOp::eNone:
        return VK_COMPARE_OP_NEVER;
    case RxSamplerCompareOp::eEqual:
        return VK_COMPARE_OP_EQUAL;
    case RxSamplerCompareOp::eLess:
        return VK_COMPARE_OP_LESS;
    case RxSamplerCompareOp::eGreater:
        return VK_COMPARE_OP_GREATER;
    case RxSamplerCompareOp::eLessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RxSamplerCompareOp::eGreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    }
}

RxSampler::RxSampler(RxSampler&& other)
{
    Sampler = other.Sampler;
    mDevice = other.mDevice;

    other.Sampler = nullptr;
    other.mDevice = nullptr;
}

RxSampler::RxSampler(RxSamplerFilter min_filter, RxSamplerFilter mag_filter, RxSamplerFilter mipmap_filter,
                     RxSamplerAddressMode address_mode, RxSamplerBorderColor border_color,
                     RxSamplerCompareOp compare_op)
{
    Create(min_filter, mag_filter, mipmap_filter, address_mode, border_color, compare_op);
}

void RxSampler::Create(RxSamplerFilter min_filter, RxSamplerFilter mag_filter, RxSamplerFilter mipmap_filter,
                       RxSamplerAddressMode address_mode, RxSamplerBorderColor border_color,
                       RxSamplerCompareOp compare_op)
{
    if (Sampler) {
        OldLog::Warning("Sampler has been previously initialized!", 0);
        return;
    }

    mDevice = gRenderer->GetDevice();

    VkSamplerAddressMode vk_addr_mode = GetVkAddressMode(address_mode);


    VkSamplerCreateInfo sampler_info {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

        .magFilter = GetVkFilter(mag_filter),
        .minFilter = GetVkFilter(min_filter),

        .mipmapMode = GetVkMipMode(mipmap_filter),

        .addressModeU = vk_addr_mode,
        .addressModeV = vk_addr_mode,
        .addressModeW = vk_addr_mode,

        .mipLodBias = 0.0f,

        .anisotropyEnable = VK_FALSE,
        // .maxAnisotropy = 0,

        .compareEnable = static_cast<VkBool32>(compare_op != RxSamplerCompareOp::eNone),
        .compareOp = GetVkCompareOp(compare_op),

        .minLod = 0.0f,
        .maxLod = 0.0f,

        .borderColor = GetVkBorderColor(border_color),
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult result = vkCreateSampler(mDevice->Device, &sampler_info, nullptr, &Sampler);

    if (result != VK_SUCCESS) {
        OldLog::Error("Error creating texture sampler!", 0);
        Sampler = nullptr;
    }
}

void RxSampler::Create() { Create(RxSamplerFilter::eLinear, RxSamplerFilter::eLinear, RxSamplerFilter::eLinear); }

void RxSampler::Destroy()
{
    if (!Sampler) {
        return;
    }


    vkDestroySampler(mDevice->Device, Sampler, nullptr);
    Sampler = nullptr;
}
