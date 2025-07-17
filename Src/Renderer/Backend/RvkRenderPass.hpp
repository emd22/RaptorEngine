#pragma once

#include <vulkan/vulkan.h>

#include <Core/FxSlice.hpp>

class RvkSwapchain;
class RvkGpuDevice;
class RvkCommandBuffer;

class RvkRenderPass
{
public:
    // void Create(RvkGpuDevice &device, RvkSwapchain &swapchain);
    // void CreateComp(RvkGpuDevice& device, RvkSwapchain& swapchain);

    void Create2(const FxSlice<VkAttachmentDescription>& color_attachments);

    void Begin(RvkCommandBuffer* cmd, VkFramebuffer framebuffer, const FxSlice<VkClearValue>& clear_colors);
    // void BeginComp(RvkCommandBuffer* cmd);

    void End();

    void Destroy();

    ~RvkRenderPass()
    {
        Destroy();
    }

public:
    VkRenderPass RenderPass = nullptr;
    RvkCommandBuffer* CommandBuffer = nullptr;

private:
    RvkGpuDevice* mDevice = nullptr;
};
