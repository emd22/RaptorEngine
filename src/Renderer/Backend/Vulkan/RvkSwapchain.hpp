#pragma once

#include "Renderer/Backend/Vulkan/Pipeline.hpp"
#include "Renderer/Backend/Vulkan/Pipeline.hpp"
#include <vulkan/vulkan.h>

#include "RvkImage.hpp"

#include "Framebuffer.hpp"

#include <Core/StaticArray.hpp>
#include <Math/Vector.hpp>

class GPUDevice;

namespace vulkan {

class RvkSwapchain {
public:
    RvkSwapchain() = default;
    ~RvkSwapchain();

    void Init(Vec2u size, VkSurfaceKHR &surface, RvkGpuDevice *device);
    void CreateSwapchainFramebuffers(GraphicsPipeline *pipeline);

    VkSwapchainKHR GetSwapchain() { return mSwapchain; }

    void Destroy();

private:
    void CreateSwapchain(Vec2u size, VkSurfaceKHR &surface);
    void CreateSwapchainImages();
    void CreateImageViews();

    void DestroyFramebuffersAndImageViews();
    void DestroyInternalSwapchain();

public:
    StaticArray<VkImageView> ImageViews;
    StaticArray<VkImage> Images;

    StaticArray<RvkImage> DepthImages;

    StaticArray<Framebuffer> Framebuffers;

    Vec2u Extent = Vec2u::Zero;

    VkSurfaceFormatKHR SurfaceFormat;

    bool Initialized = false;

private:
    RvkGpuDevice *mDevice = nullptr;
    GraphicsPipeline *mPipeline = nullptr;
    VkSwapchainKHR mSwapchain = nullptr;
};

}; // namespace vulkan
