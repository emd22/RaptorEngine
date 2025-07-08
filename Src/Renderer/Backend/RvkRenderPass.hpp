#pragma once

#include <vulkan/vulkan.h>

class RvkSwapchain;
class RvkGpuDevice;
class RvkCommandBuffer;

class RvkRenderPass
{
public:
    void Create(RvkGpuDevice &device, RvkSwapchain &swapchain);
    void CreateComp(RvkGpuDevice& device, RvkSwapchain& swapchain);

    void Begin();
    void BeginComp();

    void End();

    void Destroy(RvkGpuDevice &device);

public:
    VkRenderPass RenderPass = nullptr;
    RvkCommandBuffer *CommandBuffer = nullptr;
};
