#include "RxSamplerCache.hpp"

#include "FxEngine.hpp"

#include <Core/FxUtil.hpp>
#include <Renderer/RxRenderBackend.hpp>

FX_SET_MODULE_NAME("RxSamplerCache")

constexpr RxSamplerPackedPropertiesType RxSamplerCache::MakePackedProperties(RxSamplerFilter min_filter,
                                                                             RxSamplerFilter mag_filter,
                                                                             RxSamplerFilter mipmap_filter)
{
    RxSamplerPackedProperties properties;

    properties.Fields.MinFilter = FxUtil::EnumToInt(min_filter);
    properties.Fields.MagFilter = FxUtil::EnumToInt(mag_filter);
    properties.Fields.MipmapFilter = FxUtil::EnumToInt(mipmap_filter);

    return properties.Value;
}

#define ToSF(value_) static_cast<RxSamplerFilter>(value_)

void RxSamplerCache::CreateSamplersWithProperties(uint32 number_of_samplers, RxSamplerPackedPropertiesType properties)
{
    RxSamplerCacheSection cache_section {};

    cache_section.mSamplers.InitCapacity(number_of_samplers);
    cache_section.SamplersInUse.InitZero(number_of_samplers);

    for (uint32 i = 0; i < number_of_samplers; i++) {
        RxSamplerPackedProperties properties_obj;
        properties_obj.Value = properties;

        RxSampler sampler(ToSF(properties_obj.Fields.MinFilter), ToSF(properties_obj.Fields.MagFilter),
                          ToSF(properties_obj.Fields.MipmapFilter));

        sampler.mDescriptorSet.Create(mDescriptorPool, mDsLayoutSampler);
        SetupSamplerDescriptor(sampler, i);

        cache_section.mSamplers.Insert(std::move(sampler));
    }

    mSamplerCache.insert({ properties, std::move(cache_section) });
}

void RxSamplerCache::SetupSamplerDescriptor(RxSampler& sampler, uint32 binding)
{
    VkDescriptorImageInfo sampler_info {
        .sampler = sampler.Sampler,
    };

    VkWriteDescriptorSet sampler_write_descriptor_set {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = sampler.mDescriptorSet,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &sampler_info,
    };

    vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, 1, &sampler_write_descriptor_set, 0, nullptr);
}


void RxSamplerCache::DestroySamplersWithProperties(RxSamplerPackedPropertiesType properties)
{
    RxSamplerCacheSection& section = mSamplerCache[properties];

    for (uint32 i = 0; i < section.mSamplers.Size; i++) {
        section.mSamplers[i].Destroy();
    }

    section.mSamplers.Clear();
}

RxSamplerHandle RxSamplerCache::RequestSampler(RxSamplerPackedPropertiesType properties)
{
    auto section_it = mSamplerCache.find(properties);

    // Section could not be found
    if (section_it == mSamplerCache.end()) {
        FxLogDebug("Could not find sampler cache section; creating new section from properties");

        CreateSamplersWithProperties(RX_SAMPLER_CACHE_FALLBACK_COUNT, properties);
    }

    RxSamplerCacheSection& section = section_it->second;

    const uint32 samplers_count = section.mSamplers.Size;

    for (uint32 i = 0; i < samplers_count; i++) {
        if (section.SamplersInUse.Get(i)) {
            continue;
        }

        section.SamplersInUse.Set(i);

        RxSamplerHandle handle { .Sampler = &section.mSamplers[i], .Index = i, .PackedProperties = properties };

        return handle;
    }

    FxLogError("Could not find a free sampler in sampler cache! -> PackedProperties: 0x{:08d}", properties);

    RxSamplerHandle handle { .Sampler = nullptr, .Index = 0, .PackedProperties = properties };
    return handle;
}

void RxSamplerCache::ReleaseSampler(const RxSamplerHandle& cache_handle)
{
    if (cache_handle.Sampler == nullptr) {
        return;
    }

    // Release the sampler
    mSamplerCache[cache_handle.PackedProperties].SamplersInUse.Unset(cache_handle.Index);
}


void RxSamplerCache::CreateSamplerDsLayout()
{
    VkDescriptorSetLayoutBinding sampler_binding {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr,
    };

    VkDescriptorSetLayoutCreateInfo sampler_layout_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &sampler_binding,
    };

    VkResult status = vkCreateDescriptorSetLayout(gRenderer->GetDevice()->Device, &sampler_layout_info, nullptr,
                                                  &mDsLayoutSampler);
    if (status != VK_SUCCESS) {
        FxModulePanicVulkan("Failed to create descriptor set layout", status);
    }
}

void RxSamplerCache::Create()
{
    mDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_SAMPLER, 30);
    mDescriptorPool.Create(gRenderer->GetDevice());

    CreateSamplerDsLayout();


    // Max sampler allocation count is around 1024 on my M2 Pro with MoltenVK, but it seems that
    // the Vulkan standard specifies 4000 as the minimum. Not sure what good values are here but there
    // won't be very many bound at a time.

    CreateSamplersWithProperties(
        RX_SAMPLER_CACHE_MAX_LINEAR,
        MakePackedProperties(RxSamplerFilter::Linear, RxSamplerFilter::Linear, RxSamplerFilter::Linear));

    CreateSamplersWithProperties(
        RX_SAMPLER_CACHE_MAX_NEAREST,
        MakePackedProperties(RxSamplerFilter::Nearest, RxSamplerFilter::Nearest, RxSamplerFilter::Nearest));

    CreateSamplersWithProperties(
        RX_SAMPLER_CACHE_MAX_LLN,
        MakePackedProperties(RxSamplerFilter::Linear, RxSamplerFilter::Linear, RxSamplerFilter::Nearest));

    CreateSamplersWithProperties(
        RX_SAMPLER_CACHE_MAX_NNL,
        MakePackedProperties(RxSamplerFilter::Nearest, RxSamplerFilter::Nearest, RxSamplerFilter::Linear));
}

void RxSamplerCache::Destroy()
{
    for (auto& it : mSamplerCache) {
        DestroySamplersWithProperties(it.first);
    }

    mSamplerCache.clear();
}


void RxSamplerHandle::Bind(const RxCommandBuffer& cmd, const RxPipeline& pipeline)
{
    Sampler->mDescriptorSet.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}
