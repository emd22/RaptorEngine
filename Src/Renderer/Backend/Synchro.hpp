#pragma once

#include <vulkan/vulkan.h>

#include <Core/Assert.hpp>
#include <Core/Bitset.hpp>
#include <Core/Types.hpp>

namespace fx::renderer {

/**
 * @brief A CPU-GPU barrier.
 */
class Fence
{
public:
	void Create();

	void WaitFor(uint64 timeout = UINT64_MAX) const;
	void Reset();

	FX_FORCE_INLINE VkFence Get() { return InternalFence; }
	FX_FORCE_INLINE const VkFence Get() const { return InternalFence; }

	void Destroy();

public:
	VkFence InternalFence = nullptr;
};


enum class eSemaphoreType
{
	Binary,
	Timeline,
};

/**
 * @brief A GPU-side barrier.
 */
class Semaphore
{
public:
	Semaphore() = default;

	void Create(eSemaphoreType semaphore_type);

	FX_FORCE_INLINE VkSemaphore Get() { return InternalSemaphore; }
	FX_FORCE_INLINE const VkSemaphore Get() const { return InternalSemaphore; }

	void Destroy();

public:
	VkSemaphore InternalSemaphore = nullptr;
	eSemaphoreType Type = eSemaphoreType::Binary;
};

} // namespace fx::renderer
