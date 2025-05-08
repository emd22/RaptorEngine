#pragma once

#include "vulkan/vulkan_core.h"
#include <vulkan/vulkan.h>

#include <Renderer/Backend/Vulkan/Device.hpp>

#include <Core/FxPanic.hpp>
#include <Core/StaticArray.hpp>
#include <vector>

namespace vulkan {

class DescriptorPool
{
public:
    void Create(GPUDevice *device, uint32 max_sets = 10)
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

public:
    VkDescriptorPool Pool = nullptr;
    std::vector<VkDescriptorPoolSize> PoolSizes;
private:
    friend class DescriptorSet;

    GPUDevice *mDevice = nullptr;
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

    operator VkDescriptorSet() const { return Set; }

    void Destroy()
    {
        vkDestroyDescriptorSetLayout(mDevice->Device, Layout, nullptr);
    }

public:
    VkDescriptorSet Set = nullptr;
    VkDescriptorSetLayout Layout = nullptr;
private:
    GPUDevice *mDevice = nullptr;
};


} // namespace vulkan
