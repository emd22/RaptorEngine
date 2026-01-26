#pragma once

#include "RxDevice.hpp"
#include "RxGpuBuffer.hpp"
#include "RxPipeline.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxPanic.hpp>
#include <Core/FxSizedArray.hpp>

#include "vulkan/vulkan_core.h"
// #include "RxTexture.hpp"
//
class RxTexture;

#include <Renderer/RxConstants.hpp>

class RxDescriptorPool
{
public:
    void Create(RxGpuDevice* device, uint32 max_sets = 10);

    bool IsInited() const { return (Pool != nullptr); }
    FX_FORCE_INLINE VkDescriptorPool Get() const { return Pool; }
    void AddPoolSize(VkDescriptorType type, uint32_t count) { RemainingDescriptorCounts[type] = count; }

    void Destroy();
    ~RxDescriptorPool() { Destroy(); }

public:
    VkDescriptorPool Pool = nullptr;

    std::unordered_map<VkDescriptorType, uint32> RemainingDescriptorCounts;

private:
    friend class RxDescriptorSet;
};


class RxDescriptorSet
{
public:
    void Create(const RxDescriptorPool& pool, VkDescriptorSetLayout layout, uint32 count = 1);

    static inline void BindMultiple(uint32 first_set_index, const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point,
                                    const RxPipeline& pipeline, VkDescriptorSet* sets, uint32 sets_count)
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, first_set_index, sets_count, sets, 0, nullptr);
    }

    static inline void BindMultiple(uint32 first_set_index, const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point,
                                    const RxPipeline& pipeline, const FxSlice<VkDescriptorSet>& sets)
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, first_set_index, sets.Size, sets.pData, 0, nullptr);
    }

    static inline void BindMultipleOffset(uint32 first_set_index, const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point,
                                          const RxPipeline& pipeline, const FxSlice<VkDescriptorSet>& sets,
                                          const FxSlice<uint32>& offsets)
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, first_set_index, sets.Size, sets.pData, offsets.Size,
                                offsets.pData);
    }

    void BindWithOffset(uint32 first_set_index, const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point, const RxPipeline& pipeline,
                        uint32 offset) const
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, first_set_index, 1, &Set, 1, &offset);
    }

    void Bind(uint32 first_set_index, const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point, const RxPipeline& pipeline) const
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, first_set_index, 1, &Set, 0, nullptr);
    }

    VkDescriptorSet Get() { return Set; }

    operator VkDescriptorSet() { return Set; }

    void Destroy() {}

    bool IsInited() const { return Set != nullptr; }

public:
    VkDescriptorSet Set = nullptr;
};
