#pragma once

#include "Framebuffer.hpp"
#include "Image.hpp"
#include "Sampler.hpp"

#include <vulkan/vulkan.h>

#include <Core/SizedArray.hpp>
#include <Math/Vec2.hpp>

namespace fx::renderer {

class GpuDevice;

class Swapchain
{
public:
    Swapchain() = default;
    ~Swapchain();

    void Init(Vec2u size, VkSurfaceKHR surface, GpuDevice* device);
    void Rebuild(Vec2u new_size, VkSurfaceKHR surface);

    VkSwapchainKHR GetSwapchain() const { return mSwapchain; }

    float GetAspectRatio() const
    {
        Assert(Extent.X > 0 && Extent.Y > 0);
        return static_cast<float>(Extent.X) / Extent.Y;
    }

    void Destroy();

private:
    void CreateSwapchain(Vec2u size, VkSurfaceKHR surface);
    void CreateSwapchainImages();
    void CreateImageViews();
    void CreateFramebuffers();
    void CreateSamplers();

    void DestroyFramebuffersAndImageViews();
    void DestroyInternalSwapchain();

public:
    SizedArray<Image> OutputImages;

    Sampler ColorSampler;
    Sampler DepthSampler;
    Sampler ShadowDepthSampler;
    Sampler NormalsSampler;
    Sampler LightsSampler;

    Vec2u Extent = Vec2u::sZero;

    // VkSurfaceFormatKHR SurfaceFormat;
    struct
    {
        eImageFormat Format = eImageFormat::None;
        VkColorSpaceKHR ColorSpace;
    } Surface;

    bool bInitialized = false;

private:
    GpuDevice* mDevice = nullptr;

    SizedArray<Framebuffer> mFramebuffers;

    // GraphicsPipeline* mPipeline = nullptr;
    // GraphicsPipeline* mCompPipeline = nullptr;
    VkSwapchainKHR mSwapchain = nullptr;
};

} // namespace fx::renderer
