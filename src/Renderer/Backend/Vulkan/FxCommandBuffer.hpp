#pragma once

#include <vulkan/vulkan.h>

#include "FxCommandPool.hpp"

namespace vulkan {

class RvkCommandBuffer
{
public:
    void Create(FxCommandPool *pool);
    void Destroy();

    void Record(VkCommandBufferUsageFlags usage_flags = 0);

    void Reset();
    void End();

    operator VkCommandBuffer() { return CommandBuffer; }

    bool IsInitialized() const
    {
        return mInitialized;
    }

private:
    void CheckInitialized() const;

public:
    VkCommandBuffer CommandBuffer = nullptr;
private:
    bool mInitialized = false;
    FxCommandPool *mCommandPool = nullptr;
    RvkGpuDevice *mDevice = nullptr;
};

}; // namespace vulkan
