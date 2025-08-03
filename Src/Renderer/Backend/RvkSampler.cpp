#include "RvkSampler.hpp"

#include <Renderer/Renderer.hpp>

void RvkSampler::Create(VkFilter min_filter, VkFilter mag_filter, VkSamplerMipmapMode mipmap_mode)
{
    mDevice = Renderer->GetDevice();

    VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .minFilter = min_filter,
        .magFilter = mag_filter,
       
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,

        .anisotropyEnable = VK_FALSE,
        // .maxAnisotropy = 0,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,

        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,

        .mipmapMode = mipmap_mode,
        .mipLodBias = 0.0f,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };

    VkResult result = vkCreateSampler(mDevice->Device, &sampler_info, nullptr, &Sampler);
    
    if (result != VK_SUCCESS) {
        Log::Error("Error creating texture sampler!", 0);
        Sampler = nullptr;
    }
}

void RvkSampler::Destroy()
{
    if (!Sampler) {
        return;
    }

    vkDestroySampler(mDevice->Device, Sampler, nullptr);
    Sampler = nullptr;
}
