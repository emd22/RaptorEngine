#pragma once

#include "Device.hpp"

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>

namespace fx::renderer {

class CommandPool
{
public:
    void Create(GpuDevice* device, uint32 queue_family);

    void Reset() { vkResetCommandPool(mpDevice->Device, CommandPool, 0); }

    FX_FORCE_INLINE const VkCommandPool Get() const { return CommandPool; }
    FX_FORCE_INLINE VkCommandPool Get() { return CommandPool; };

    void Destroy()
    {
        if (!CommandPool) {
            return;
        }

        vkDestroyCommandPool(mpDevice->Device, CommandPool, nullptr);
        CommandPool = nullptr;
    }

    ~CommandPool() { Destroy(); }

public:
    VkCommandPool CommandPool = nullptr;

    uint32 QueueFamilyIndex = 0;

private:
    GpuDevice* mpDevice = nullptr;
};

class CommandBuffer
{
public:
    void Create(CommandPool* pool);
    void Destroy();

    void Record(VkCommandBufferUsageFlags usage_flags = 0);

    void Reset();
    void End();

    FX_FORCE_INLINE const VkCommandBuffer Get() const { return CommandBuffer; }
    FX_FORCE_INLINE VkCommandBuffer Get() { return CommandBuffer; }

    operator VkCommandBuffer() const { return CommandBuffer; }

    bool IsInitialized() const { return mbInitialized; }

private:
    void CheckInitialized() const;

public:
    VkCommandBuffer CommandBuffer = nullptr;

private:
    bool mbInitialized = false;
    CommandPool* mpCommandPool = nullptr;
    GpuDevice* mpDevice = nullptr;
};

} // namespace fx::renderer
