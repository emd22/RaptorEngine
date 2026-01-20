#include "RxSynchro.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>
#include <Renderer/Backend/RxUtil.hpp>

/////////////////////////////////////
// RxFence functions
/////////////////////////////////////

void RxFence::Create()
{
    const VkFenceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                            .pNext = nullptr,
                                            .flags = VK_FENCE_CREATE_SIGNALED_BIT };

    const VkResult status = vkCreateFence(gRenderer->GetDevice()->Device, &create_info, nullptr, &Fence);
    if (status != VK_SUCCESS) {
        FxPanicVulkan("Fence", "Could not create fence", status);
    }
}

void RxFence::WaitFor(uint64 timeout) const
{
    FxAssert(Fence != nullptr);

    const VkResult status = vkWaitForFences(gRenderer->GetDevice()->Device, 1, &Fence, true, timeout);

    if (status != VK_SUCCESS) {
        FxPanicVulkan("Fence", "Could not create fence", status);
    }
}

void RxFence::Reset()
{
    FxAssert(Fence != nullptr);

    const VkResult status = vkResetFences(gRenderer->GetDevice()->Device, 1, &Fence);

    if (status != VK_SUCCESS) {
        FxPanicVulkan("Fence", "Could not reset fence", status);
    }
}

void RxFence::Destroy()
{
    vkDestroyFence(gRenderer->GetDevice()->Device, Fence, nullptr);
    Fence = nullptr;
}



/////////////////////////////////////
// RxSemaphore functions
/////////////////////////////////////

void RxSemaphore::Create()
{
    const VkSemaphoreCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0
    };

    const VkResult status = vkCreateSemaphore(gRenderer->GetDevice()->Device, &create_info, nullptr, &Semaphore);

    if (status != VK_SUCCESS) {
        FxPanicVulkan("Semaphore", "Could not create semaphore", status);
    }
}

void RxSemaphore::Destroy() { vkDestroySemaphore(gRenderer->GetDevice()->Device, Semaphore, nullptr); }




/////////////////////////////////////
// Semaphore Cache functions
/////////////////////////////////////


RxSemaphoreCache::RxSemaphoreCache()
{

}

