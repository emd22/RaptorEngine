#pragma once

#include "vulkan/vulkan_core.h"
#include <vulkan/vulkan.h>

#include "RvkDevice.hpp"

#include <Core/FxPanic.hpp>
#include <Core/FxSizedArray.hpp>
#include <vector>

#include "RvkGpuBuffer.hpp"
#include "RvkPipeline.hpp"
#include "RvkTexture.hpp"

#include <Renderer/Constants.hpp>

class RvkDescriptorPool
{
public:
    void Create(RvkGpuDevice* device, uint32 max_sets = 10);

    void AddPoolSize(VkDescriptorType type, uint32_t count)
    {
        PoolSizes.push_back({ .type = type, .descriptorCount = count });
    }

    operator VkDescriptorPool() const { return Pool; }

    void Destroy()
    {
        vkDestroyDescriptorPool(mDevice->Device, Pool, nullptr);
    }

public:
    VkDescriptorPool Pool = nullptr;
    std::vector<VkDescriptorPoolSize> PoolSizes;
private:
    friend class RvkDescriptorSet;

    RvkGpuDevice *mDevice = nullptr;
};

class RvkDescriptorSet
{
public:
    void Create(const RvkDescriptorPool &pool, VkDescriptorSetLayout layout);

    // template <typename T>
    // void WriteBuffer(VkBuffer& buffer)
    // {

    // }

    template <typename T>
    VkWriteDescriptorSet GetBufferWriteDescriptor(uint32 bind_dest, const RvkRawGpuBuffer<T>& buffer, VkDescriptorType type)
    {
        VkDescriptorBufferInfo info{
            .buffer = buffer.Buffer,
            .offset = 0,
            .range = sizeof(T)
        };

        VkWriteDescriptorSet write{
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

    VkWriteDescriptorSet GetImageWriteDescriptor(uint32 bind_dest, const RvkTexture& texture, VkImageLayout layout, VkDescriptorType type);

    void SubmitWrites(FxSizedArray<VkWriteDescriptorSet>& to_write)
    {
        vkUpdateDescriptorSets(mDevice->Device, to_write.Size, to_write.Data, 0, nullptr);
        // mDescriptorWrites.Clear();
    }

    static void BindMultiple(
        const RvkCommandBuffer& cmd,
        VkPipelineBindPoint bind_point,
        const RvkGraphicsPipeline& pipeline,
        VkDescriptorSet* sets,
        uint32 sets_count)
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, 0, sets_count, sets, 0, nullptr);
    }

    void Bind(const RvkCommandBuffer &cmd, VkPipelineBindPoint bind_point, const RvkGraphicsPipeline &pipeline) const
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, 0, 1, &Set, 0, nullptr);
    }

    operator VkDescriptorSet() { return Set; }

    void Destroy()
    {
    }

    bool IsInited() const
    {
        return Set != nullptr;
    }

    // ~DescriptorSet()
    // {
    //     Destroy();
    // }

public:
    VkDescriptorSet Set = nullptr;
private:
    RvkGpuDevice *mDevice = nullptr;
};
