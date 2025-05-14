#pragma once

#include "Renderer/Backend/Vulkan/RvkPipeline.hpp"
#include "Renderer/Backend/Vulkan/RvkPipeline.hpp"
#include <vulkan/vulkan.h>

#include "RvkImage.hpp"

#include "RvkFramebuffer.hpp"

#include <Core/StaticArray.hpp>
#include <Math/Vector.hpp>

class GPUDevice;

namespace vulkan {

class RvkSwapchain {
public:
    RvkSwapchain() = default;
    ~RvkSwapchain();

    void Init(Vec2u size, VkSurfaceKHR &surface, RvkGpuDevice *device);
    void CreateSwapchainFramebuffers(RvkGraphicsPipeline *pipeline);

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

    StaticArray<RvkFramebuffer> Framebuffers;

    Vec2u Extent = Vec2u::Zero;

    VkSurfaceFormatKHR SurfaceFormat;

    bool Initialized = false;

private:
    RvkGpuDevice *mDevice = nullptr;
    RvkGraphicsPipeline *mPipeline = nullptr;
    VkSwapchainKHR mSwapchain = nullptr;
};

}; // namespace vulkan
