#pragma once

#include "RxDevice.hpp"

#include <vulkan/vulkan.h>

#include <Core/Log.hpp>

enum class RxSamplerFilter
{
    eNearest,
    eLinear,
};

enum class RxSamplerAddressMode
{
    eRepeat,
    eClampToBorder,
};

enum class RxSamplerBorderColor
{
    eIntBlack,
    eFloatBlack,
    eIntWhite,
    eFloatWhite,
    eIntTransparent,
    eFloatTransparent,
};

enum class RxSamplerCompareOp
{
    eNone,
    eEqual,
    eLess,
    eGreater,
    eLessOrEqual,
    eGreaterOrEqual,
};


class RxSampler
{
public:
    RxSampler() = default;
    RxSampler(RxSamplerFilter min_filter, RxSamplerFilter mag_filter, RxSamplerFilter mipmap_filter,
              RxSamplerAddressMode address_mode = RxSamplerAddressMode::eRepeat,
              RxSamplerBorderColor border_color = RxSamplerBorderColor::eIntBlack,
              RxSamplerCompareOp = RxSamplerCompareOp::eNone);

    RxSampler(RxSampler&& other);

    void Create(RxSamplerFilter min_filter, RxSamplerFilter mag_filter, RxSamplerFilter mipmap_filter,
                RxSamplerAddressMode address_mode = RxSamplerAddressMode::eRepeat,
                RxSamplerBorderColor border_color = RxSamplerBorderColor::eIntBlack,
                RxSamplerCompareOp compare_op = RxSamplerCompareOp::eNone);

    void Create();

    void Destroy();

    ~RxSampler() { Destroy(); }

public:
    VkSampler Sampler = nullptr;

private:
    friend class RxSamplerCache;
    friend struct RxSamplerHandle;

    RxGpuDevice* mDevice = nullptr;
};
