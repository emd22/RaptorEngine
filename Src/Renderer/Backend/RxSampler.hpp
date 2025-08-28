#pragma once

#include "RxDescriptors.hpp"
#include "RxDevice.hpp"

#include <vulkan/vulkan.h>

#include <Core/Log.hpp>

enum class RxSamplerFilter
{
    Nearest,
    Linear,
};


class RxSampler
{
public:
    RxSampler() = default;
    RxSampler(RxSamplerFilter min_filter, RxSamplerFilter mag_filter, RxSamplerFilter mipmap_filter);

    RxSampler(RxSampler&& other);

    void Create(RxSamplerFilter min_filter, RxSamplerFilter mag_filter, RxSamplerFilter mipmap_filter);
    void Create();

    void Destroy();

    ~RxSampler() { Destroy(); }

public:
    VkSampler Sampler = nullptr;

private:
    friend class RxSamplerCache;
    friend struct RxSamplerHandle;

    RxGpuDevice* mDevice = nullptr;

    RxDescriptorSet mDescriptorSet {};
};
