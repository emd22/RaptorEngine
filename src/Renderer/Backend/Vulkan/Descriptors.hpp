#pragma once

#include "vulkan/vulkan_core.h"
#include <vulkan/vulkan.h>

#include <Renderer/Backend/Vulkan/Device.hpp>

#include <Core/FxPanic.hpp>
#include <Core/StaticArray.hpp>
#include <vector>

#include <Renderer/Backend/Vulkan/FxGpuBuffer.hpp>
#include <Renderer/Backend/Vulkan/Pipeline.hpp>

namespace vulkan {

class DescriptorPool
{
public:
    void Create(RvkGpuDevice *device, uint32 max_sets = 10)
    {
        mDevice = device;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = max_sets;
        pool_info.poolSizeCount = PoolSizes.size();
        pool_info.pPoolSizes = PoolSizes.data();

        if (vkCreateDescriptorPool(device->Device, &pool_info, nullptr, &Pool) != VK_SUCCESS) {
            FxPanic_("DescriptorPool", "Failed to create descriptor pool!", 0);
        }
    }

    void AddPoolSize(VkDescriptorType type, uint32_t count)
    {
        PoolSizes.push_back({ .type = type, .descriptorCount = count });
    }

    operator VkDescriptorPool() const { return Pool; }

    void Destroy()
    {
        vkDestroyDescriptorPool(mDevice->Device, Pool, nullptr);
    }

    // ~DescriptorPool()
    // {
    //     Destroy();
    // }

public:
    VkDescriptorPool Pool = nullptr;
    std::vector<VkDescriptorPoolSize> PoolSizes;
private:
    friend class DescriptorSet;

    RvkGpuDevice *mDevice = nullptr;
};

class DescriptorSet
{
public:
    void Create(DescriptorPool &pool, VkDescriptorSetLayout layout)
    {
        mDevice = pool.mDevice;

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = pool.Pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &layout;

        if (vkAllocateDescriptorSets(pool.mDevice->Device, &alloc_info, &Set) != VK_SUCCESS) {
            FxPanic_("DescriptorSet", "Failed to allocate descriptor set!", 0);
        }
    }

    template <typename T>
    void SetBuffer(FxRawGpuBuffer<T> &buffer, VkDescriptorType type)
    {
        VkDescriptorBufferInfo info{
            .buffer = buffer.Buffer,
            .offset = 0,
            .range = sizeof(T)
        };

        VkWriteDescriptorSet desc_write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = type,
            .descriptorCount = 1,
            .dstSet = Set,
            .dstBinding = BindingDest,
            .dstArrayElement = 0,
            .pBufferInfo = &info,
        };

        vkUpdateDescriptorSets(mDevice->Device, 1, &desc_write, 0, nullptr);
    }

    void Bind(RvkCommandBuffer &cmd, VkPipelineBindPoint bind_point, GraphicsPipeline &pipeline) const
    {
        vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout, 0, 1, &Set, 0, nullptr);
    }

    operator VkDescriptorSet() { return Set; }

    void Destroy()
    {
        vkDestroyDescriptorSetLayout(mDevice->Device, Layout, nullptr);
    }

    // ~DescriptorSet()
    // {
    //     Destroy();
    // }

public:
    VkDescriptorSet Set = nullptr;
    VkDescriptorSetLayout Layout = nullptr;
    uint32 BindingDest = 0;
private:
    RvkGpuDevice *mDevice = nullptr;
};


} // namespace vulkan
