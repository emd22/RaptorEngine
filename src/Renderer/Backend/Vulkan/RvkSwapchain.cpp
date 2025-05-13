#include "RvkSwapchain.hpp"
#include "Core/Defines.hpp"
#include "Device.hpp"

#include <Core/FxPanic.hpp>

#include "Renderer/Backend/Vulkan/Pipeline.hpp"
#include "Renderer/Renderer.hpp"
#include "vulkan/vulkan_core.h"

#include <vulkan/vulkan.h>

FX_SET_MODULE_NAME("RvkSwapchain")

namespace vulkan {

void RvkSwapchain::Init(Vec2u size, VkSurfaceKHR &surface, RvkGpuDevice *device)
{
    AssertRendererExists();

    mDevice = device;

    CreateSwapchain(size, surface);
    CreateSwapchainImages();
    CreateImageViews();

    Initialized = true;
}

void RvkSwapchain::CreateSwapchainImages()
{
    uint32 image_count;

    vkGetSwapchainImagesKHR(mDevice->Device, mSwapchain, &image_count, nullptr);

    Images.InitSize(image_count);
    vkGetSwapchainImagesKHR(mDevice->Device, mSwapchain, &image_count, Images.Data);

    DepthImages.InitSize(image_count);
}

void RvkSwapchain::CreateImageViews()
{
    const uint64 images_size = Images.Size;

    ImageViews.InitSize(images_size);

    for (int32 i = 0; i < images_size; i++) {
        const VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = Images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = SurfaceFormat.format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };

        VkResult status = vkCreateImageView(mDevice->Device, &create_info, nullptr, &ImageViews[i]);
        if (status != VK_SUCCESS) {
            FxPanic("Could not create swapchain image view", status);
        }

        RvkImage &depth_image = DepthImages[i];

        depth_image.Create(
            Extent,
            VK_FORMAT_D16_UNORM,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );
    }
}

void RvkSwapchain::CreateSwapchain(Vec2u size, VkSurfaceKHR &surface)
{
    Extent = size;

    VkSurfaceCapabilitiesKHR capabilities;
    const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mDevice->Physical, surface, &capabilities);

    if (result != VK_SUCCESS) {
        FxPanic("Error retrieving surface capabilities", result);
    }

    const VkExtent2D extent = {
        .width = (uint32)size.GetX(),
        .height = (uint32)size.GetY(),
    };

    uint32 image_count = capabilities.minImageCount + 1;

    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    Log::Info("Swapchain - Min:%d, Max:%d, Selected:%d", capabilities.minImageCount, capabilities.maxImageCount, image_count);

    SurfaceFormat = mDevice->GetBestSurfaceFormat();

    const VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,

        .minImageCount = image_count,

        .imageFormat = SurfaceFormat.format,
        .imageColorSpace = SurfaceFormat.colorSpace,

        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,

        .presentMode = present_mode,
        .preTransform = capabilities.currentTransform,

        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .clipped = VK_TRUE,
        // mSwapchain is null if not initialized
        .oldSwapchain = mSwapchain,
    };

    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = nullptr;

    const VkResult status = vkCreateSwapchainKHR(mDevice->Device, &create_info, nullptr, &mSwapchain);

    if (status != VK_SUCCESS) {
        FxPanic("Could not create swapchain", status);
    }
}

void RvkSwapchain::CreateSwapchainFramebuffers(GraphicsPipeline *pipeline)
{
    Log::Debug("Image view count: %d", ImageViews.Size);
    Framebuffers.Free();
    Framebuffers.InitSize(ImageViews.Size);

    StaticArray<VkImageView> temp_views;
    temp_views.InitSize(2);

    for (int i = 0; i < ImageViews.Size; i++) {
        temp_views[0] = ImageViews[i];
        temp_views[1] = DepthImages[i].View;

        Framebuffers[i].Create(temp_views, *pipeline, Extent);
    }

    Log::Debug("Create framebuffers", 0);

    mPipeline = pipeline;
}

void RvkSwapchain::DestroyFramebuffersAndImageViews()
{
    // Destroy and free depth images
    for (RvkImage &depth_image : DepthImages) {
        depth_image.Destroy();
    }
    DepthImages.Free();

    for (int i = 0; i < ImageViews.Size; i++) {
        Framebuffers[i].Destroy();
        vkDestroyImageView(mDevice->Device, ImageViews[i], nullptr);
    }

    Framebuffers.Free();
    ImageViews.Free();
}

void RvkSwapchain::DestroyInternalSwapchain()
{
    vkDestroySwapchainKHR(mDevice->Device, mSwapchain, nullptr);
}

void RvkSwapchain::Destroy()
{
    DestroyFramebuffersAndImageViews();
    Images.Free();
    DestroyInternalSwapchain();

    Initialized = false;
}

RvkSwapchain::~RvkSwapchain()
{
    Images.Free();
}

} // namespace vulkan
