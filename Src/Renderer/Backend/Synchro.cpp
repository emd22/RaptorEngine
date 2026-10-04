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
	uint64 handle = 0;
	const VkResult status = static_cast<VkResult>(
		rx_gpu_fence_create(gGraphics->GetDevice()->GetRustDevice(), true, &handle));

	if (status != VK_SUCCESS) {
		PanicVulkan("Fence", "Could not create fence", status);
	}

	InternalFence = RxFromRaw<VkFence>(handle);
}

void Fence::WaitFor(uint64 timeout) const
{
	Assert(InternalFence != nullptr);

	const VkResult status = static_cast<VkResult>(
		rx_gpu_fence_wait(gGraphics->GetDevice()->GetRustDevice(), RxRaw(InternalFence), timeout));

	if (status != VK_SUCCESS) {
		PanicVulkan("Fence", "Could not create fence", status);
	}
}

void Fence::Reset()
{
	Assert(InternalFence != nullptr);

	const VkResult status = static_cast<VkResult>(
		rx_gpu_fence_reset(gGraphics->GetDevice()->GetRustDevice(), RxRaw(InternalFence)));

	if (status != VK_SUCCESS) {
		PanicVulkan("Fence", "Could not reset fence", status);
	}
}

void Fence::Destroy()
{
	if (InternalFence == nullptr) {
		return;
	}

	rx_gpu_fence_destroy(gGraphics->GetDevice()->GetRustDevice(), RxRaw(InternalFence));
	InternalFence = nullptr;
}


/////////////////////////////////////
// Semaphore functions
/////////////////////////////////////

void Semaphore::Create(eSemaphoreType semaphore_type)
{
	uint64 handle = 0;
	const VkResult status = static_cast<VkResult>(rx_gpu_semaphore_create(
		gGraphics->GetDevice()->GetRustDevice(), semaphore_type == eSemaphoreType::Timeline, &handle));

	if (status != VK_SUCCESS) {
		PanicVulkan("Semaphore", "Could not create semaphore", status);
	}

	InternalSemaphore = RxFromRaw<VkSemaphore>(handle);
}

void Semaphore::Destroy()
{
	if (InternalSemaphore != nullptr) {
		rx_gpu_semaphore_destroy(gGraphics->GetDevice()->GetRustDevice(), RxRaw(InternalSemaphore));
		InternalSemaphore = nullptr;
	}
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
