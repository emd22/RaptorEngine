#pragma once

#include "Core/StaticArray.hpp"
#include "vulkan/vulkan_core.h"
#include <Core/Types.hpp>

#include <cstdint>
#include <vulkan/vulkan.h>

#include <vector>

namespace vulkan {

class QueueFamilies {
public:
    static const uint32 QueueNull = UINT32_MAX;

    void FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

    /// Checks if the main graphics and presentation queues have been queried
    bool IsComplete()
    {
        return (mGraphicsIndex != QueueNull
             && mPresentIndex != QueueNull);
    }

    /// Retrieves the available queue indices
    std::vector<uint32> GetQueueIndexList() const
    {
        return std::vector<uint32>({
            mGraphicsIndex,
            mPresentIndex,
        });
    }

    /// Checks if the graphics queue is also the presentation queue
    bool IsGraphicsAlsoPresent() const
    {
        return (mGraphicsIndex == mPresentIndex);
    }

    uint32 GetGraphicsFamily()
    {
        return mGraphicsIndex;
    }

    uint32 GetPresentFamily()
    {
        return mPresentIndex;
    }

public:
    StaticArray<VkQueueFamilyProperties> RawFamilies;
private:
    uint32 mGraphicsIndex = QueueNull;
    uint32 mPresentIndex = QueueNull;
};

class GPUDevice {
public:
    GPUDevice() = default;
    GPUDevice(VkInstance instance, VkSurfaceKHR surface)
    {
        Create(instance, surface);
    }

    void Create(VkInstance instance, VkSurfaceKHR surface);
    void Destroy();

    void PickPhysicalDevice();
    void CreateLogicalDevice();

    void WaitForIdle();

    VkSurfaceFormatKHR GetBestSurfaceFormat();

private:
    bool IsPhysicalDeviceSuitable(VkPhysicalDevice &device);
    void QueryQueues();

public:
    VkPhysicalDevice Physical = nullptr;
    VkDevice Device = nullptr;

    QueueFamilies mQueueFamilies;

    VkQueue GraphicsQueue = nullptr;
    VkQueue PresentQueue = nullptr;

private:
    VkInstance mInstance;
    VkSurfaceKHR mSurface;
};

}; // namespace vulkan
