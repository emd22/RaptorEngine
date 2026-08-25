#include "Synchro.hpp"

#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

/////////////////////////////////////
// Fence functions
/////////////////////////////////////

void Fence::Create()
{
	const VkFenceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
											.pNext = nullptr,
											.flags = VK_FENCE_CREATE_SIGNALED_BIT };

	const VkResult status = vkCreateFence(gGraphics->GetDevice()->Device, &create_info, nullptr, &InternalFence);
	if (status != VK_SUCCESS) {
		PanicVulkan("Fence", "Could not create fence", status);
	}
}

void Fence::WaitFor(uint64 timeout) const
{
	Assert(InternalFence != nullptr);

	const VkResult status = vkWaitForFences(gGraphics->GetDevice()->Device, 1, &InternalFence, true, timeout);

	if (status != VK_SUCCESS) {
		PanicVulkan("Fence", "Could not create fence", status);
	}
}

void Fence::Reset()
{
	Assert(InternalFence != nullptr);

	const VkResult status = vkResetFences(gGraphics->GetDevice()->Device, 1, &InternalFence);

	if (status != VK_SUCCESS) {
		PanicVulkan("Fence", "Could not reset fence", status);
	}
}

void Fence::Destroy()
{
	if (InternalFence == nullptr) {
		return;
	}
	vkDestroyFence(gGraphics->GetDevice()->Device, InternalFence, nullptr);
	InternalFence = nullptr;
}


/////////////////////////////////////
// Semaphore functions
/////////////////////////////////////

void Semaphore::Create(eSemaphoreType semaphore_type)
{
	VkSemaphoreType sem_type = VK_SEMAPHORE_TYPE_BINARY;
	switch (semaphore_type) {
	case eSemaphoreType::Binary:
		sem_type = VK_SEMAPHORE_TYPE_BINARY;
		break;
	case eSemaphoreType::Timeline:
		sem_type = VK_SEMAPHORE_TYPE_TIMELINE;
		break;
	default:;
	}

	const VkSemaphoreTypeCreateInfo type_create_info {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = sem_type,
		.initialValue = 0,
	};

	const VkSemaphoreCreateInfo create_info {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &type_create_info,
		.flags = 0,
	};

	const VkResult status = vkCreateSemaphore(gGraphics->GetDevice()->Device, &create_info, nullptr,
											  &InternalSemaphore);

	if (status != VK_SUCCESS) {
		PanicVulkan("Semaphore", "Could not create semaphore", status);
	}
}

void Semaphore::Destroy()
{
	vkDestroySemaphore(gGraphics->GetDevice()->Device, InternalSemaphore, nullptr);
	InternalSemaphore = nullptr;
}


/////////////////////////////////////
// Semaphore Cache functions
/////////////////////////////////////


// SemaphoreCache::SemaphoreCache()
// {
// 	mSemaphores.InitCapacity(scNumSemaphores);
// 	mInUse.InitZero(scNumSemaphores);
// }

// Semaphore* SemaphoreCache::Request()
// {
// 	uint32 next_free = mInUse.FindNextFreeBit();

// 	// No available semaphores, return null
// 	if (next_free == Bitset::scNoFreeBits) {
// 		return nullptr;
// 	}

// 	Semaphore* semaphore = nullptr;

// 	// If there are semaphores available but they have not been created yet, create one
// 	if (next_free > mSemaphores.Size) {
// 		semaphore = mSemaphores.Insert();
// 		semaphore->Create();
// 		semaphore->SetCacheId(next_free);
// 	}
// 	else {
// 		semaphore = &mSemaphores[next_free];
// 	}

// 	return semaphore;
// }

// void SemaphoreCache::Release(Semaphore* semaphore) { mInUse.Unset(semaphore->GetCacheId()); }

// SemaphoreCache::~SemaphoreCache()
// {
// 	for (Semaphore& sem : mSemaphores) {
// 		sem.Destroy();
// 	}

// 	mSemaphores.Free();
// }

} // namespace fx::renderer
