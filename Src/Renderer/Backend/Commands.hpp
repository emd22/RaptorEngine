#pragma once

#include "Device.hpp"

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>

namespace fx::renderer {

class CommandPool
{
public:
    void Create(GpuDevice* device, uint32 queue_family);

    void Reset() { rx_gpu_command_pool_reset(mpDevice->GetRustDevice(), RxRaw(CmdPool)); }

    FX_FORCE_INLINE const VkCommandPool Get() const { return CmdPool; }
    FX_FORCE_INLINE VkCommandPool Get() { return CmdPool; };

    void Destroy()
    {
        if (!CmdPool) {
            return;
        }

        rx_gpu_command_pool_destroy(mpDevice->GetRustDevice(), RxRaw(CmdPool));
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

    FX_FORCE_INLINE uint32 QueueFamily() const { return mpCommandPool->QueueFamilyIndex; }

    operator VkCommandBuffer() const { return Cmd; }

    bool IsInitialized() const { return mbInitialized; }

    /// The pipeline most recently bound in this command buffer, so a repeated bind can be skipped
    mutable VkPipeline pBoundPipeline = nullptr;

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
