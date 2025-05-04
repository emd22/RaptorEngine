#pragma once

#include <vulkan/vulkan.h>

namespace vulkan {

class Swapchain;
class GPUDevice;
class FxCommandBuffer;

class RenderPass
{
public:
    void Create(GPUDevice &device, Swapchain &swapchain);

    void Begin();
    void End();

    void Destroy(GPUDevice &device);

public:
    VkRenderPass RenderPass = nullptr;
    FxCommandBuffer *CommandBuffer = nullptr;
};

}; // namespace vulkan
