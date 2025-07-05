#pragma once

#include "RvkPipeline.hpp"
#include "RvkImage.hpp"
#include "RvkFramebuffer.hpp"

#include <Core/FxSizedArray.hpp>
#include <Math/Vector.hpp>

#include <vulkan/vulkan.h>

class RvkGpuDevice;

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
    // FxSizedArray<VkImageView> ImageViews;
    // FxSizedArray<VkImage> Images;

    FxSizedArray<RvkImage> Images;
    FxSizedArray<RvkImage> DepthImages;
    FxSizedArray<RvkImage> PositionImages;

    FxSizedArray<RvkFramebuffer> Framebuffers;

    Vec2u Extent = Vec2u::Zero;

    VkSurfaceFormatKHR SurfaceFormat;

    bool Initialized = false;

private:
    RvkGpuDevice *mDevice = nullptr;
    RvkGraphicsPipeline *mPipeline = nullptr;
    VkSwapchainKHR mSwapchain = nullptr;
};
