#include "RxDescriptors.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>

/////////////////////////////////////
// Descriptor Pool Functions
/////////////////////////////////////

void RxDescriptorPool::Create(RxGpuDevice* device, uint32 max_sets)
{
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

void RxDescriptorPool::Destroy()
{
    if (!Pool) {
        return;
    }

    vkDestroyDescriptorPool(gRenderer->GetDevice()->Device, Pool, nullptr);
    Pool = nullptr;
}


void RxDescriptorSet::Create(const RxDescriptorPool& pool, VkDescriptorSetLayout layout, uint32 count)
{
    FxAssertMsg(pool.IsInited(), "Descriptor pool is not initialized!");

    VkDescriptorSetAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool.Get();
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VkResult status = vkAllocateDescriptorSets(gRenderer->GetDevice()->Device, &alloc_info, &Set);

    if (status != VK_SUCCESS) {
        FxPanicVulkan("DescriptorSet", "Failed to allocate descriptor set!", status);
    }
}
