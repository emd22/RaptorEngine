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

    void AddPoolSize(VkDescriptorType type, uint32_t count) { RemainingDescriptorCounts[type] = count; }

    void Destroy()
    {
        if (!Pool) {
            return;
        }

        vkDestroyDescriptorPool(mDevice->Device, Pool, nullptr);
        Pool = nullptr;
    }

    ~RxDescriptorPool() { Destroy(); }

public:
    VkDescriptorPool Pool = nullptr;

    std::unordered_map<VkDescriptorType, uint32> RemainingDescriptorCounts;

private:
    friend class RxDescriptorSet;

    RxGpuDevice* mDevice = nullptr;
};


class RxDescriptorSet
{
public:
    void Create(const RxDescriptorPool& pool, VkDescriptorSetLayout layout, uint32 count = 1);

    // template <typename T>
    // void WriteBuffer(VkBuffer& buffer)
    // {

    // }

    template <typename T>
    VkWriteDescriptorSet GetBufferWriteDescriptor(uint32 bind_dest, const RxRawGpuBuffer<T>& buffer,
                                                  VkDescriptorType type)
    {
        VkDescriptorBufferInfo info { .buffer = buffer.Buffer, .offset = 0, .range = sizeof(T) };

        VkWriteDescriptorSet write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = type,
            .descriptorCount = 1,
            .dstSet = Set,
            .dstBinding = bind_dest,
            .dstArrayElement = 0,
            .pBufferInfo = &info,
        };

        return write;
    }

    VkWriteDescriptorSet GetImageWriteDescriptor(uint32 bind_dest, const RxTexture& texture, VkImageLayout layout,
                                                 VkDescriptorType type);

    void SubmitWrites(FxSizedArray<VkWriteDescriptorSet>& to_write)
    {
        vkUpdateDescriptorSets(mDevice->Device, to_write.Size, to_write.pData, 0, nullptr);
        // mDescriptorWrites.Clear();
    }

    static inline void BindMultiple(const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point,
                                    const RxPipeline& pipeline, VkDescriptorSet* sets, uint32 sets_count)
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, 0, sets_count, sets, 0, nullptr);
    }

    static inline void BindMultiple(const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point,
                                    const RxPipeline& pipeline, const FxSlice<VkDescriptorSet>& sets)
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, 0, sets.Size, sets.pData, 0, nullptr);
    }

    static inline void BindMultipleOffset(const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point,
                                          const RxPipeline& pipeline, const FxSlice<VkDescriptorSet>& sets,
                                          const FxSlice<uint32>& offsets)
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, 0, sets.Size, sets.pData, offsets.Size,
                                offsets.pData);
    }

    void BindWithOffset(const RxCommandBuffer &cmd, VkPipelineBindPoint bind_point, const RxPipeline &pipeline, uint32 offset) const
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, 0, 1, &Set, 1, &offset);
    }

    void Bind(const RxCommandBuffer& cmd, VkPipelineBindPoint bind_point, const RxPipeline& pipeline) const
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, 0, 1, &Set, 0, nullptr);
    }

    operator VkDescriptorSet() { return Set; }

    void Destroy() {}

    bool IsInited() const { return Set != nullptr; }

    // ~DescriptorSet()
    // {
    //     Destroy();
    // }

public:
    VkDescriptorSet Set = nullptr;

private:
    RxGpuDevice* mDevice = nullptr;
};


// class RxDescriptorManager
// {
//     static constexpr uint32 scMaxPools = 5;

// public:
//     RxDescriptorManager() = default;

//     void Create();
//     void Request();

// private:
//     FxStackArray<RxDescriptorPool, scMaxPools> mPools;
// };
