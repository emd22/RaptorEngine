#include "Sampler.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

static FX_FORCE_INLINE VkSamplerMipmapMode GetVkMipMode(SamplerFilter filter)
{
    switch (filter) {
    case SamplerFilter::Nearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case SamplerFilter::Linear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

static FX_FORCE_INLINE VkFilter GetVkFilter(SamplerFilter filter)
{
    switch (filter) {
    case SamplerFilter::Nearest:
        return VK_FILTER_NEAREST;
    case SamplerFilter::Linear:
        return VK_FILTER_LINEAR;
    }

    return VK_FILTER_LINEAR;
}

static FX_FORCE_INLINE VkBorderColor GetVkBorderColor(SamplerBorderColor color)
{
    switch (color) {
    case SamplerBorderColor::IntBlack:
        return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case SamplerBorderColor::FloatBlack:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    case SamplerBorderColor::IntWhite:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    case SamplerBorderColor::FloatWhite:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;

    case SamplerBorderColor::IntTransparent:
        return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case SamplerBorderColor::FloatTransparent:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}

static FX_FORCE_INLINE VkSamplerAddressMode GetVkAddressMode(SamplerAddressMode addr_mode)
{
    switch (addr_mode) {
    case SamplerAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
}

static FX_FORCE_INLINE VkCompareOp GetVkCompareOp(SamplerCompareOp op)
{
    switch (op) {
    case SamplerCompareOp::None:
        return VK_COMPARE_OP_NEVER;
    case SamplerCompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case SamplerCompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case SamplerCompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case SamplerCompareOp::LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case SamplerCompareOp::GreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    }
}

Sampler::Sampler(Sampler&& other)
{
    Sampler = other.Sampler;
    CacheId = other.CacheId;
    CachePropId = other.CachePropId;

    other.Sampler = nullptr;
    other.InvalidateCacheId();
}

Sampler::Sampler(const SamplerProps& props) { Create(props); }

void Sampler::Create(const SamplerProps& props)
{
    if (Sampler) {
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

        .compareEnable = static_cast<VkBool32>(props.CompareOp != SamplerCompareOp::None),
        .compareOp = GetVkCompareOp(props.CompareOp),

        .minLod = 0.0f,
        .maxLod = 0.0f,

        .borderColor = GetVkBorderColor(props.BorderColor),
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult result = vkCreateSampler(gRenderer->GetDevice()->Device, &sampler_info, nullptr, &Sampler);

    if (result != VK_SUCCESS) {
        LogError("Error creating texture sampler!");
        Sampler = nullptr;
    }
}

void Sampler::Create() { Create({}); }

void Sampler::Destroy()
{
    if (!Sampler) {
        return;
    }


    vkDestroySampler(gRenderer->GetDevice()->Device, Sampler, nullptr);
    Sampler = nullptr;
}

} // namespace fx::renderer
