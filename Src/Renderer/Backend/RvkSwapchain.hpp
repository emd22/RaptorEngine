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

    void Init(Vec2u size, VkSurfaceKHR& surface, RvkGpuDevice* device);
    void CreateSwapchainFramebuffers(RvkGraphicsPipeline* pipeline, RvkGraphicsPipeline* comp_pipeline);

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
    FxSizedArray<RvkImage> OutputImages;

    FxSizedArray<RvkImage> AlbedoImages;
    FxSizedArray<RvkImage> PositionImages;
    FxSizedArray<RvkImage> DepthImages;

    FxSizedArray<RvkFramebuffer> GPassFramebuffers;
    FxSizedArray<RvkFramebuffer> CompFramebuffers;

    Vec2u Extent = Vec2u::Zero;

    VkSurfaceFormatKHR SurfaceFormat;

    bool Initialized = false;

private:
    RvkGpuDevice *mDevice = nullptr;
    RvkGraphicsPipeline* mPipeline = nullptr;
    RvkGraphicsPipeline* mCompPipeline = nullptr;
    VkSwapchainKHR mSwapchain = nullptr;
};
