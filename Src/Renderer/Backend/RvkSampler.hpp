#pragma once

#include <vulkan/vulkan.h>

#include <Core/Log.hpp>

#include <Renderer/Backend/RvkDevice.hpp>

class RvkSampler
{
public:
    void Create(VkFilter min_filter = VK_FILTER_LINEAR, VkFilter mag_filter = VK_FILTER_LINEAR, VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR);
    void Destroy();

    ~RvkSampler()
    {
        Destroy();
    }

public:
    VkSampler Sampler = nullptr;

private:
    RvkGpuDevice* mDevice = nullptr;
};
