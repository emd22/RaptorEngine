#pragma once

#include <vulkan/vulkan.h>

#include <Core/FxSlice.hpp>
#include <Renderer/RxAttachment.hpp>

class RxSwapchain;
class RxGpuDevice;
class RxCommandBuffer;

class RxRenderPass
{
public:
    // void Create(RxGpuDevice &device, RxSwapchain &swapchain);
    // void CreateComp(RxGpuDevice& device, RxSwapchain& swapchain);

    void Create(RxAttachmentList& color_attachments);

    void Begin(RxCommandBuffer* cmd, VkFramebuffer framebuffer, const FxSlice<VkClearValue>& clear_colors);
    // void BeginComp(RxCommandBuffer* cmd);

    void End();

    void Destroy();

    ~RxRenderPass() { Destroy(); }

public:
    VkRenderPass RenderPass = nullptr;
    RxCommandBuffer* CommandBuffer = nullptr;

private:
    RxGpuDevice* mDevice = nullptr;
};
