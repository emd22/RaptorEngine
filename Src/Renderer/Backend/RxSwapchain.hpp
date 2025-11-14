#pragma once

#include "RxFramebuffer.hpp"
#include "RxImage.hpp"
#include "RxPipeline.hpp"
#include "RxSampler.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxSizedArray.hpp>
#include <Math/Vector.hpp>

class RxGpuDevice;

class RxSwapchain
{
public:
    RxSwapchain() = default;
    ~RxSwapchain();

    void Init(FxVec2u size, VkSurfaceKHR& surface, RxGpuDevice* device);
    void CreateSwapchainFramebuffers();

    VkSwapchainKHR GetSwapchain() { return mSwapchain; }

    void Destroy();

private:
    void CreateSwapchain(FxVec2u size, VkSurfaceKHR& surface);
    void CreateSwapchainImages();
    void CreateImageViews();

    void DestroyFramebuffersAndImageViews();
    void DestroyInternalSwapchain();

public:
    // FxSizedArray<VkImageView> ImageViews;
    // FxSizedArray<VkImage> Images;
    FxSizedArray<RxImage> OutputImages;

    // FxSizedArray<RxImage> ColorImages;
    // FxSizedArray<RxImage> PositionImages;
    // FxSizedArray<RxImage> DepthImages;

    RxSampler ColorSampler;
    RxSampler DepthSampler;
    RxSampler NormalsSampler;
    RxSampler LightsSampler;

    // FxSizedArray<RxFramebuffer> GPassFramebuffers;
    // FxSizedArray<RxFramebuffer> CompFramebuffers;

    FxVec2u Extent = FxVec2u::sZero;

    VkSurfaceFormatKHR SurfaceFormat;

    bool Initialized = false;

private:
    RxGpuDevice* mDevice = nullptr;
    // RxGraphicsPipeline* mPipeline = nullptr;
    // RxGraphicsPipeline* mCompPipeline = nullptr;
    VkSwapchainKHR mSwapchain = nullptr;
};
