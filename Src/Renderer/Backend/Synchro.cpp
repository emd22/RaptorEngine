#include "Synchro.hpp"

#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

/////////////////////////////////////
// Fence functions
/////////////////////////////////////

void Fence::Create()
{
    const VkFenceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                            .pNext = nullptr,
                                            .flags = VK_FENCE_CREATE_SIGNALED_BIT };

    const VkResult status = vkCreateFence(gRenderer->GetDevice()->Device, &create_info, nullptr, &Fence);
    if (status != VK_SUCCESS) {
        PanicVulkan("Fence", "Could not create fence", status);
    }
}

void Fence::WaitFor(uint64 timeout) const
{
    Assert(Fence != nullptr);

    const VkResult status = vkWaitForFences(gRenderer->GetDevice()->Device, 1, &Fence, true, timeout);

    if (status != VK_SUCCESS) {
        PanicVulkan("Fence", "Could not create fence", status);
    }
}

void Fence::Reset()
{
    Assert(Fence != nullptr);

    const VkResult status = vkResetFences(gRenderer->GetDevice()->Device, 1, &Fence);

    if (status != VK_SUCCESS) {
        PanicVulkan("Fence", "Could not reset fence", status);
    }
}

void Fence::Destroy()
{
    vkDestroyFence(gRenderer->GetDevice()->Device, Fence, nullptr);
    Fence = nullptr;
}


/////////////////////////////////////
// Semaphore functions
/////////////////////////////////////

void Semaphore::Create()
{
    const VkSemaphoreCreateInfo create_info { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                              .pNext = nullptr,
                                              .flags = 0 };

    const VkResult status = vkCreateSemaphore(gRenderer->GetDevice()->Device, &create_info, nullptr,
                                              &InternalSemaphore);

    if (status != VK_SUCCESS) {
        PanicVulkan("Semaphore", "Could not create semaphore", status);
    }
}

void Semaphore::Destroy()
{
    vkDestroySemaphore(gRenderer->GetDevice()->Device, InternalSemaphore, nullptr);
    InternalSemaphore = nullptr;
}


/////////////////////////////////////
// Semaphore Cache functions
/////////////////////////////////////


SemaphoreCache::SemaphoreCache()
{
    mSemaphores.InitCapacity(scNumSemaphores);
    mInUse.InitZero(scNumSemaphores);
}

Semaphore* SemaphoreCache::Request()
{
    uint32 next_free = mInUse.FindNextFreeBit();

    // No available semaphores, return null
    if (next_free == Bitset::scNoFreeBits) {
        return nullptr;
    }

    Semaphore* semaphore = nullptr;

    // If there are semaphores available but they have not been created yet, create one
    if (next_free > mSemaphores.Size) {
        semaphore = mSemaphores.Insert();
        semaphore->Create();
        semaphore->SetCacheId(next_free);
    }
    else {
        semaphore = &mSemaphores[next_free];
    }

    return semaphore;
}

void SemaphoreCache::Release(Semaphore* semaphore) { mInUse.Unset(semaphore->GetCacheId()); }

SemaphoreCache::~SemaphoreCache()
{
    for (Semaphore& sem : mSemaphores) {
        sem.Destroy();
    }

    mSemaphores.Free();
}

} // namespace fx::renderer
