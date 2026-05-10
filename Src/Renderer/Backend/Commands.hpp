#pragma once

#include "Device.hpp"

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>

namespace fx::renderer {

class CommandPool
{
public:
    void Create(GpuDevice* device, uint32 queue_family);

    void Reset() { vkResetCommandPool(mpDevice->Device, CmdPool, 0); }

    FX_FORCE_INLINE const VkCommandPool Get() const { return CmdPool; }
    FX_FORCE_INLINE VkCommandPool Get() { return CmdPool; };

    void Destroy()
    {
        if (!CmdPool) {
            return;
        }

        vkDestroyCommandPool(mpDevice->Device, CmdPool, nullptr);
        CmdPool = nullptr;
    }

    ~CommandPool() { Destroy(); }

public:
    VkCommandPool CmdPool = nullptr;

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

    FX_FORCE_INLINE const VkCommandBuffer Get() const { return Cmd; }
    FX_FORCE_INLINE VkCommandBuffer Get() { return Cmd; }

    operator VkCommandBuffer() const { return Cmd; }

    bool IsInitialized() const { return mbInitialized; }

private:
    void CheckInitialized() const;

public:
    VkCommandBuffer Cmd = nullptr;

private:
    bool mbInitialized = false;
    CommandPool* mpCommandPool = nullptr;
    GpuDevice* mpDevice = nullptr;
};

} // namespace fx::renderer
