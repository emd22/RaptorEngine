#pragma once

#include <vulkan/vulkan.h>

#include <Core/FxPanic.hpp>
#include <Core/FxTypes.hpp>

/**
 * @brief A CPU-GPU barrier.
 */
class RxFence
{
public:
    void Create();

    void WaitFor(uint64 timeout = UINT64_MAX) const;
    void Reset();

    FX_FORCE_INLINE VkFence Get() const { return Fence; }

    void Destroy();

public:
    VkFence Fence = nullptr;
};


/**
 * @brief A GPU-side barrier.
 */
class RxSemaphore
{
public:
    void Create();

    FX_FORCE_INLINE VkSemaphore Get() const { return Semaphore; }

    void Destroy();

public:
    VkSemaphore Semaphore;
};


class RxSemaphoreCache
{
public:
    RxSemaphoreCache();
};