#pragma once

#include "RvkPipeline.hpp"
#include "RvkImage.hpp"
#include "RvkFramebuffer.hpp"
#include "RvkSampler.hpp"

#include <Core/FxSizedArray.hpp>
#include <Math/Vector.hpp>

#include <vulkan/vulkan.h>

class RvkGpuDevice;

class RvkSwapchain {
public:
    RvkSwapchain() = default;
    ~RvkSwapchain();

    void Init(FxVec2u size, VkSurfaceKHR& surface, RvkGpuDevice* device);
    void CreateSwapchainFramebuffers();

    VkSwapchainKHR GetSwapchain() { return mSwapchain; }

    void Destroy();

private:
    void CreateSwapchain(FxVec2u size, VkSurfaceKHR &surface);
    void CreateSwapchainImages();
    void CreateImageViews();

    void DestroyFramebuffersAndImageViews();
    void DestroyInternalSwapchain();

public:
    // FxSizedArray<VkImageView> ImageViews;
    // FxSizedArray<VkImage> Images;
    FxSizedArray<RvkImage> OutputImages;

    // FxSizedArray<RvkImage> ColorImages;
    // FxSizedArray<RvkImage> PositionImages;
    // FxSizedArray<RvkImage> DepthImages;

    RvkSampler ColorSampler;
    RvkSampler DepthSampler;
    RvkSampler NormalsSampler;
    RvkSampler LightsSampler;

    // FxSizedArray<RvkFramebuffer> GPassFramebuffers;
    // FxSizedArray<RvkFramebuffer> CompFramebuffers;

    FxVec2u Extent = FxVec2u::Zero;

    VkSurfaceFormatKHR SurfaceFormat;

    bool Initialized = false;

private:
    RvkGpuDevice *mDevice = nullptr;
    // RvkGraphicsPipeline* mPipeline = nullptr;
    // RvkGraphicsPipeline* mCompPipeline = nullptr;
    VkSwapchainKHR mSwapchain = nullptr;
};
