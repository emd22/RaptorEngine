#pragma once

#include "Backend/RxDescriptors.hpp"
#include "Backend/RxSampler.hpp"

#include <Core/FxBitset.hpp>
#include <Core/FxHash.hpp>
#include <map>

#define RX_SAMPLER_CACHE_MAX_LINEAR  5
#define RX_SAMPLER_CACHE_MAX_NEAREST 5

#define RX_SAMPLER_CACHE_MAX_LLN 3
#define RX_SAMPLER_CACHE_MAX_NNL 3

#define RX_SAMPLER_CACHE_FALLBACK_COUNT 3

using RxSamplerPackedPropertiesType = uint64;

union RxSamplerPackedProperties
{
    struct
    {
        /* 0 */
        uint8 MinFilter : 2;

        /* 2 */
        uint8 MagFilter : 2;

        /* 4 */
        uint8 MipmapFilter : 2;

        /* 6 */

        // TODO: Add anisotropy, compare ops when added
    } Fields;

    RxSamplerPackedPropertiesType Value;
};

struct RxSamplerHandle
{
    RxSampler* Sampler = nullptr;

    uint32 Index = 0;
    RxSamplerPackedPropertiesType PackedProperties = 0;

public:
    void Bind(const RxCommandBuffer& cmd, const RxPipeline& pipeline);
};


struct RxSamplerCacheSection
{
    FxSizedArray<RxSampler> mSamplers;
    FxBitset SamplersInUse;
};

class RxSamplerCache
{
public:
    void Create();

    constexpr RxSamplerPackedPropertiesType MakePackedProperties(RxSamplerFilter min_filter, RxSamplerFilter mag_filter,
                                                                 RxSamplerFilter mipmap_filter);

    RxSamplerHandle RequestSampler(RxSamplerPackedPropertiesType properties);
    void ReleaseSampler(const RxSamplerHandle& cache_handle);

    void Destroy();

    ~RxSamplerCache() { Destroy(); }

private:
    void CreateSamplersWithProperties(uint32 number_of_samplers, RxSamplerPackedPropertiesType properties);
    void DestroySamplersWithProperties(RxSamplerPackedPropertiesType properties);

    void SetupSamplerDescriptor(RxSampler& sampler, uint32 binding);

    void CreateSamplerDsLayout();

private:
    std::unordered_map<RxSamplerPackedPropertiesType, RxSamplerCacheSection> mSamplerCache;

    RxDescriptorPool mDescriptorPool;

    VkDescriptorSetLayout mDsLayoutSampler { nullptr };
};
