#pragma once

#include <vulkan/vulkan.h>

#include "CommandPool.hpp"

namespace vulkan {

class CommandBuffer
{
public:
    void Create(CommandPool *pool);
    void Destroy();

    void Record();
    void Reset();
    void End();

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
    CommandPool *mCommandPool = nullptr;
    GPUDevice *mDevice = nullptr;
};

}; // namespace vulkan
