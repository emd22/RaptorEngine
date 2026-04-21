#pragma once

#include "Device.hpp"

#include <vulkan/vulkan.h>

#include <Core/Hash.hpp>

namespace fx::renderer {

enum class eSamplerFilter : uint8
{
    Nearest,
    Linear,
};

enum class eSamplerAddressMode : uint8
{
    Repeat,
    ClampToBorder,
};

enum class eSamplerBorderColor : uint8
{
    IntBlack,
    FloatBlack,
    IntWhite,
    FloatWhite,
    IntTransparent,
    FloatTransparent,
};

enum class eSamplerCompareOp : uint8
{
    None,
    Equal,
    Less,
    Greater,
    LessOrEqual,
    GreaterOrEqual,
};

struct SamplerProps
{
    eSamplerFilter MinFilter = eSamplerFilter::Linear;
    eSamplerFilter MagFilter = eSamplerFilter::Linear;
    eSamplerFilter MipFilter = eSamplerFilter::Linear;

    eSamplerAddressMode AddressMode = eSamplerAddressMode::Repeat;
    eSamplerBorderColor BorderColor = eSamplerBorderColor::IntBlack;
    eSamplerCompareOp CompareOp = eSamplerCompareOp::None;
};


class Sampler
{
public:
    Sampler() = default;
    Sampler(const SamplerProps& props);

    Sampler(Sampler&& other);

    void Create(const SamplerProps& props);

    void Create();


    void SetCacheId(uint64 prop_id, uint32 cache_id)
    {
        CachePropId = prop_id;
        CacheId = cache_id;
    }

    void InvalidateCacheId()
    {
        CachePropId = HashNull64;
        CacheId = UINT32_MAX;
    }

    void Destroy();

    FX_FORCE_INLINE VkSampler Get() const { return InternalSampler; }

    ~Sampler() { Destroy(); }

public:
    VkSampler InternalSampler = nullptr;

    /**
     * @brief The index for the sampler in the cache.
     */
    uint32 CacheId = UINT32_MAX;

    /**
     * @brief An identifier for the properties that the sampler was created with. Used by SamplerCache.
     */
    Hash64 CachePropId = HashNull64;

private:
    friend struct SamplerHandle;
};

} // namespace fx::renderer
