#include "RxDescriptors.hpp"

#include "RxTexture.hpp"


/////////////////////////////////////
// Descriptor Pool Functions
/////////////////////////////////////

void RxDescriptorPool::Create(RxGpuDevice* device, uint32 max_sets)
{
    mDevice = device;

    const uint32 pool_sizes_count = RemainingDescriptorCounts.size();
    FxSizedArray<VkDescriptorPoolSize> pool_sizes(pool_sizes_count);

    for (const auto& desc_count : RemainingDescriptorCounts) {
        pool_sizes.Insert({ .type = desc_count.first, .descriptorCount = desc_count.second });
    }


    VkDescriptorPoolCreateInfo pool_info {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = max_sets;
    pool_info.poolSizeCount = pool_sizes.Size;
    pool_info.pPoolSizes = pool_sizes.pData;

    VkResult status = vkCreateDescriptorPool(device->Device, &pool_info, nullptr, &Pool);

    if (status != VK_SUCCESS) {
        FxPanicVulkan("DescriptorPool", "Failed to create descriptor pool!", status);
    }
}

void RxDescriptorSet::Create(const RxDescriptorPool& pool, VkDescriptorSetLayout layout, uint32 count)
{
    mDevice = pool.mDevice;

    VkDescriptorSetAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool.Pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VkResult status = vkAllocateDescriptorSets(pool.mDevice->Device, &alloc_info, &Set);

    if (status != VK_SUCCESS) {
        FxPanicVulkan("DescriptorSet", "Failed to allocate descriptor set!", status);
    }
}

VkWriteDescriptorSet RxDescriptorSet::GetImageWriteDescriptor(uint32 bind_dest, const RxTexture& texture,
                                                              VkImageLayout layout, VkDescriptorType type)
{
    VkDescriptorImageInfo image_info {
        .sampler = texture.Sampler->Sampler,
        .imageView = texture.Image.View,
        .imageLayout = layout,
    };

    VkWriteDescriptorSet write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = Set,
        .dstBinding = bind_dest,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = type,
        .pImageInfo = &image_info,
    };

    return write;
}


///////////////////////////////////////
// RxDescriptorManager
///////////////////////////////////////

// void RxDescriptorManager::Create() {}
