#include "Sampler.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

static FX_FORCE_INLINE VkSamplerMipmapMode GetVkMipMode(eSamplerFilter filter)
{
    switch (filter) {
    case eSamplerFilter::Nearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case eSamplerFilter::Linear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

static FX_FORCE_INLINE VkFilter GetVkFilter(eSamplerFilter filter)
{
    switch (filter) {
    case eSamplerFilter::Nearest:
        return VK_FILTER_NEAREST;
    case eSamplerFilter::Linear:
        return VK_FILTER_LINEAR;
    }

    return VK_FILTER_LINEAR;
}

static FX_FORCE_INLINE VkBorderColor GetVkBorderColor(eSamplerBorderColor color)
{
    switch (color) {
    case eSamplerBorderColor::IntBlack:
        return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case eSamplerBorderColor::FloatBlack:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    case eSamplerBorderColor::IntWhite:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    case eSamplerBorderColor::FloatWhite:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;

    case eSamplerBorderColor::IntTransparent:
        return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case eSamplerBorderColor::FloatTransparent:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}

static FX_FORCE_INLINE VkSamplerAddressMode GetVkAddressMode(eSamplerAddressMode addr_mode)
{
    switch (addr_mode) {
    case eSamplerAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case eSamplerAddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
}

static FX_FORCE_INLINE VkCompareOp GetVkCompareOp(eSamplerCompareOp op)
{
    switch (op) {
    case eSamplerCompareOp::None:
        return VK_COMPARE_OP_NEVER;
    case eSamplerCompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case eSamplerCompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case eSamplerCompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case eSamplerCompareOp::LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case eSamplerCompareOp::GreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    }
}

Sampler::Sampler(Sampler&& other)
{
    InternalSampler = other.InternalSampler;
    CacheId = other.CacheId;
    CachePropId = other.CachePropId;

    other.InternalSampler = nullptr;
    other.InvalidateCacheId();
}

Sampler::Sampler(const SamplerProps& props) { Create(props); }

void Sampler::Create(const SamplerProps& props)
{
    if (InternalSampler) {
        LogWarning("Sampler has been previously initialized!");
        return;
    }

    VkSamplerAddressMode vk_addr_mode = GetVkAddressMode(props.AddressMode);


    VkSamplerCreateInfo sampler_info {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

        .magFilter = GetVkFilter(props.MagFilter),
        .minFilter = GetVkFilter(props.MinFilter),

        .mipmapMode = GetVkMipMode(props.MipFilter),

        .addressModeU = vk_addr_mode,
        .addressModeV = vk_addr_mode,
        .addressModeW = vk_addr_mode,

        .mipLodBias = 0.0f,

        .anisotropyEnable = VK_FALSE,
        // .maxAnisotropy = 0,

        .compareEnable = static_cast<VkBool32>(props.CompareOp != eSamplerCompareOp::None),
        .compareOp = GetVkCompareOp(props.CompareOp),

        .minLod = 0.0f,
        .maxLod = 0.0f,

        .borderColor = GetVkBorderColor(props.BorderColor),
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult result = vkCreateSampler(gRenderer->GetDevice()->Device, &sampler_info, nullptr, &InternalSampler);

    if (result != VK_SUCCESS) {
        LogError("Error creating texture sampler!");
        InternalSampler = nullptr;
    }
}

void Sampler::Create() { Create({}); }

void Sampler::Destroy()
{
    if (!InternalSampler) {
        return;
    }


    vkDestroySampler(gRenderer->GetDevice()->Device, InternalSampler, nullptr);
    InternalSampler = nullptr;
}

} // namespace fx::renderer
