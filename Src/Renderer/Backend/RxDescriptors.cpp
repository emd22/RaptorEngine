#include "RxDescriptors.hpp"

#include "RxTexture.hpp"


/////////////////////////////////////
// Descriptor Pool Functions
/////////////////////////////////////

void RxDescriptorPool::Create(RxGpuDevice* device, uint32 max_sets)
{
    mDevice = device;

    VkDescriptorPoolCreateInfo pool_info {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = max_sets;
    pool_info.poolSizeCount = PoolSizes.size();
    pool_info.pPoolSizes = PoolSizes.data();

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
        .imageLayout = layout,
        .imageView = texture.Image.View,
        .sampler = texture.Sampler->Sampler,
    };

    VkWriteDescriptorSet write {
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
