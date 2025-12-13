#include "RxSampler.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>


static constexpr VkSamplerMipmapMode FilterToMipmapMode(RxSamplerFilter filter)
{
    switch (filter) {
    case RxSamplerFilter::Nearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case RxSamplerFilter::Linear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

static constexpr VkFilter FilterToVkFilter(RxSamplerFilter filter)
{
    switch (filter) {
    case RxSamplerFilter::Nearest:
        return VK_FILTER_NEAREST;
    case RxSamplerFilter::Linear:
        return VK_FILTER_LINEAR;
    }

    return VK_FILTER_LINEAR;
}

RxSampler::RxSampler(RxSampler&& other)
{
    Sampler = other.Sampler;
    mDevice = other.mDevice;

    other.Sampler = nullptr;
    other.mDevice = nullptr;
}

RxSampler::RxSampler(RxSamplerFilter min_filter, RxSamplerFilter mag_filter, RxSamplerFilter mipmap_filter)
{
    Create(min_filter, mag_filter, mipmap_filter);
}

void RxSampler::Create(RxSamplerFilter min_filter, RxSamplerFilter mag_filter, RxSamplerFilter mipmap_filter)
{
    if (Sampler) {
        OldLog::Warning("Sampler has been previously initialized!", 0);
        return;
    }

    mDevice = gRenderer->GetDevice();


    VkSamplerCreateInfo sampler_info {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

        .magFilter = FilterToVkFilter(mag_filter),
        .minFilter = FilterToVkFilter(min_filter),

        .mipmapMode = FilterToMipmapMode(mipmap_filter),

        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,

        .mipLodBias = 0.0f,

        .anisotropyEnable = VK_FALSE,
        // .maxAnisotropy = 0,

        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        
        .minLod = 0.0f,
        .maxLod = 0.0f,
        
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult result = vkCreateSampler(mDevice->Device, &sampler_info, nullptr, &Sampler);

    if (result != VK_SUCCESS) {
        OldLog::Error("Error creating texture sampler!", 0);
        Sampler = nullptr;
    }
}

void RxSampler::Create() { Create(RxSamplerFilter::Linear, RxSamplerFilter::Linear, RxSamplerFilter::Linear); }

void RxSampler::Destroy()
{
    if (!Sampler) {
        return;
    }

    if (mDescriptorSet.IsInited()) {
        mDescriptorSet.Destroy();
    }


    vkDestroySampler(mDevice->Device, Sampler, nullptr);
    Sampler = nullptr;
}
