#include "RvkDescriptors.hpp"


/////////////////////////////////////
// Descriptor Pool Functions
/////////////////////////////////////

void RvkDescriptorPool::Create(RvkGpuDevice* device, uint32 max_sets)
{

    mDevice = device;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = max_sets;
    pool_info.poolSizeCount = PoolSizes.size();
    pool_info.pPoolSizes = PoolSizes.data();

    if (vkCreateDescriptorPool(device->Device, &pool_info, nullptr, &Pool) != VK_SUCCESS) {
        FxPanic("DescriptorPool", "Failed to create descriptor pool!", 0);
    }

}

void RvkDescriptorSet::Create(const RvkDescriptorPool& pool, VkDescriptorSetLayout layout)
{
    mDevice = pool.mDevice;

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool.Pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(pool.mDevice->Device, &alloc_info, &Set) != VK_SUCCESS) {
        FxPanic("DescriptorSet", "Failed to allocate descriptor set!", 0);
    }
}

VkWriteDescriptorSet RvkDescriptorSet::GetImageWriteDescriptor(uint32 bind_dest, const RvkTexture& texture, VkImageLayout layout, VkDescriptorType type)
{

    VkDescriptorImageInfo image_info{
        .imageLayout = layout,
        .imageView = texture.Image.View,
        .sampler = texture.Sampler
    };

    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .descriptorType = type,
        .descriptorCount = 1,
        .dstSet = Set,
        .dstBinding = bind_dest,
        .dstArrayElement = 0,
        .pImageInfo = &image_info,
    };

    return write;
}
