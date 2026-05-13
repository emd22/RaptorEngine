#pragma once

#include "Device.hpp"
#include "GpuBuffer.hpp"
#include "Pipeline.hpp"
#include "Sampler.hpp"

#include <vulkan/vulkan.h>

#include <Core/Panic.hpp>
#include <Core/SizedArray.hpp>
#include <Renderer/Constants.hpp>

#include "vulkan/vulkan_core.h"

namespace fx::renderer {

struct Target;

class DescriptorPool
{
public:
    void Create(GpuDevice* device, uint32 max_sets = 10);

    bool IsInited() const { return (Pool != nullptr); }
    FX_FORCE_INLINE VkDescriptorPool Get() const { return Pool; }
    void AddPoolSize(VkDescriptorType type, uint32_t count) { RemainingDescriptorCounts[type] = count; }

    void Recreate();

    void Destroy();
    ~DescriptorPool() { Destroy(); }

public:
    VkDescriptorPool Pool = nullptr;

    uint16 SetCapacity = 0;
    uint16 SetsUsed = 0;

    std::unordered_map<VkDescriptorType, uint32> RemainingDescriptorCounts;

private:
    friend class DescriptorSet;
};

enum class eDescriptorBufferType
{
    Storage,
    Uniform,
};


class DescriptorSet
{
private:
    struct DescriptorEntry
    {
        uint32 BindIndex = 0;
        Image* pImage = nullptr;
        Sampler* pSampler = nullptr;
        RawGpuBuffer* pBuffer = nullptr;

        uint64 BufferOffset = 0;
        uint64 BufferRange = 0;
    };


    static constexpr uint32 scMaxBuffers = 2;
    static constexpr uint32 scMaxImages = 6;

    static constexpr uint32 scMaxDescriptorEntries = scMaxBuffers + scMaxImages;

public:
    void Create(DescriptorPool& pool, VkDescriptorSetLayout layout, bool has_dynamic_offsets, uint32 count = 1);
    bool IsInited() const { return Set != nullptr; }

    static void BindMultiple(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
                             const Pipeline& pipeline, VkDescriptorSet* sets, uint32 sets_count);

    static void BindMultiple(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
                             const Pipeline& pipeline, const Slice<VkDescriptorSet>& sets);

    static void BindMultipleOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
                                   const Pipeline& pipeline, const Slice<VkDescriptorSet>& sets,
                                   const Slice<uint32>& offsets);

    void BindWithOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
                        const Pipeline& pipeline, uint32 offset) const;

    void Bind(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
              const Pipeline& pipeline) const;

    void AddBuffer(uint32 bind_index, RawGpuBuffer* buffer, uint64 offset, uint64 range);
    void AddImage(uint32 bind_index, Image* image, Sampler* sampler);
    void AddImageFromTarget(uint32 bind_index, Target* target, Sampler* sampler);

    void Build();

    VkDescriptorSet Get()
    {
        if (!mbIsBuilt) {
            Build();
        }

        return Set;
    }

    bool HasDynamicOffsets() const { return mbHasDynamicOffsets; }

    VkDescriptorSetLayout GetLayout() { return Layout; }
    void DestroyLayout();

    bool operator!() const { return Set == nullptr; }

    void Destroy();

    ~DescriptorSet() { Destroy(); }

private:
    VkDescriptorSet Set = nullptr;
    VkDescriptorSetLayout Layout = nullptr;

    uint32 NumBuffers = 0;

    bool mbHasDynamicOffsets : 1 = false;
    bool mbIsBuilt : 1 = false;

    SizedArray<DescriptorEntry> mDescriptorEntries;
};

} // namespace fx::renderer
