#pragma once

#include "Core/SizedArray.hpp"

#include <vulkan/vulkan.h>

#include <Core/LockContext.hpp>
#include <Core/Types.hpp>
#include <cstdint>
#include <vector>

namespace fx::renderer {

class RxQueueFamilies
{
public:
    static const uint32 scNullQueue = UINT32_MAX;

    void FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

    /// Checks if the main graphics and presentation queues have been queried
    bool IsComplete()
    {
        return (mGraphicsIndex != scNullQueue && mPresentIndex != scNullQueue && mTransferIndex != scNullQueue);
    }

    /// Retrieves the available queue indices
    std::vector<uint32> GetQueueIndexList() const
    {
        return std::vector<uint32>({
            mGraphicsIndex,
            mPresentIndex,

            mTransferIndex,
        });
    }

    FX_FORCE_INLINE bool HasIndependentTransfer() const { return mTransferIndex != mGraphicsIndex; }

    uint32 GetGraphicsFamily() const { return mGraphicsIndex; }
    uint32 GetPresentFamily() const { return mPresentIndex; }
    uint32 GetTransferFamily() const { return mTransferIndex; }

private:
    void FindGraphicsFamily(VkPhysicalDevice device, VkSurfaceKHR surface);
    void FindTransferFamily(VkPhysicalDevice device, VkSurfaceKHR surface);

    bool FamilyHasPresentSupport(VkPhysicalDevice device, VkSurfaceKHR surface, uint32 family_index);

public:
    SizedArray<VkQueueFamilyProperties> RawFamilies;

private:
    uint32 mGraphicsIndex = scNullQueue;
    uint32 mPresentIndex = scNullQueue;
    uint32 mTransferIndex = scNullQueue;
};

class RxGpuDevice
{
public:
    RxGpuDevice() = default;
    RxGpuDevice(VkInstance instance, VkSurfaceKHR surface) { Create(instance, surface); }

    void Create(VkInstance instance, VkSurfaceKHR surface);
    void Destroy();

    void PickPhysicalDevice();
    void CreateLogicalDevice();

    void WaitForIdle();

    VkSurfaceFormatKHR GetSurfaceFormat();

    SpinLockContext<VkQueue> GetLockableQueue(VkQueue queue)
    {
        if (mQueueFamilies.HasIndependentTransfer()) {
            return SpinLockContext<VkQueue>(mTransferMutex, queue, true);
        }

        return SpinLockContext<VkQueue>(mTransferMutex, queue);
    }

    FX_FORCE_INLINE SpinLockContext<VkQueue> GetGraphicsQueue() { return GetLockableQueue(mGraphicsQueue); }
    FX_FORCE_INLINE SpinLockContext<VkQueue> GetTransferQueue() { return GetLockableQueue(mTransferQueue); }
    FX_FORCE_INLINE SpinLockContext<VkQueue> GetPresentQueue() { return GetLockableQueue(mPresentQueue); }

    operator VkDevice() const { return Device; }
    operator VkPhysicalDevice() const { return Physical; }

private:
    bool IsPhysicalDeviceSuitable(VkPhysicalDevice& device);
    void QueryQueues();

public:
    VkPhysicalDevice Physical = nullptr;
    VkDevice Device = nullptr;

    RxQueueFamilies mQueueFamilies;


private:
    VkInstance mInstance;
    VkSurfaceKHR mSurface;

    VkQueue mGraphicsQueue = nullptr;
    VkQueue mTransferQueue = nullptr;
    VkQueue mPresentQueue = nullptr;

    std::atomic_flag mTransferMutex;
};

} // namespace fx::renderer
