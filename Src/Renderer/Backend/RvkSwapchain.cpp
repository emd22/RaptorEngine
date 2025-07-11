#include "RvkSwapchain.hpp"
#include "RvkDevice.hpp"
#include "RvkPipeline.hpp"
#include "../Renderer.hpp"

#include <Core/Defines.hpp>
#include <Core/FxPanic.hpp>

#include <vulkan/vulkan.h>

FX_SET_MODULE_NAME("RvkSwapchain")

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

    // Images.InitSize(image_count);

    FxSizedArray<VkImage> raw_images;
    raw_images.InitSize(image_count);

    vkGetSwapchainImagesKHR(mDevice->Device, mSwapchain, &image_count, raw_images.Data);

    OutputImages.InitCapacity(image_count);

    for (VkImage& raw_image : raw_images) {
        RvkImage* image = OutputImages.Insert();
        image->Image = raw_image;
        image->View = nullptr;
        image->Allocation = nullptr;
        image->ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image->Format = SurfaceFormat.format;
    }

    // ColorImages.InitSize(image_count);
    // DepthImages.InitSize(image_count);
    // PositionImages.InitSize(image_count);
}

void RvkSwapchain::CreateImageViews()
{
    for (int32 i = 0; i < OutputImages.Size; i++) {
        const VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = OutputImages[i].Image,
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

        VkResult status = vkCreateImageView(mDevice->Device, &create_info, nullptr, &OutputImages[i].View);
        if (status != VK_SUCCESS) {
            FxModulePanic("Could not create swapchain image view", status);
        }


        // DepthImages[i].Create(
        //     Extent,
        //     VK_FORMAT_D16_UNORM,
        //     VK_IMAGE_TILING_OPTIMAL,
        //     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        //     VK_IMAGE_ASPECT_DEPTH_BIT
        // );

        // PositionImages[i].Create(
        //     Extent,
        //     VK_FORMAT_B8G8R8A8_UNORM,
        //     VK_IMAGE_TILING_OPTIMAL,
        //     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        //     VK_IMAGE_ASPECT_COLOR_BIT
        // );

        // ColorImages[i].Create(
        //     Extent,
        //     VK_FORMAT_B8G8R8A8_UNORM,
        //     VK_IMAGE_TILING_OPTIMAL,
        //     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT  | VK_IMAGE_USAGE_SAMPLED_BIT,
        //     VK_IMAGE_ASPECT_COLOR_BIT
        // );
    }
}

void RvkSwapchain::CreateSwapchain(Vec2u size, VkSurfaceKHR &surface)
{
    Extent = size;

    VkSurfaceCapabilitiesKHR capabilities;
    const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mDevice->Physical, surface, &capabilities);

    if (result != VK_SUCCESS) {
        FxModulePanic("Error retrieving surface capabilities", result);
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
        FxModulePanic("Could not create swapchain", status);
    }
}

void RvkSwapchain::CreateSwapchainFramebuffers(RvkGraphicsPipeline* comp_pipeline)
{
    // Log::Debug("Image view count: %d", ColorImages.Size);
    // GPassFramebuffers.Free();
    // GPassFramebuffers.InitSize(ColorImages.Size);

    // FxSizedArray<VkImageView> temp_views;
    // temp_views.InitSize(3);

    // for (int i = 0; i < ColorImages.Size; i++) {
    //     temp_views[0] = ColorImages[i].View;
    //     temp_views[1] = PositionImages[i].View;
    //     temp_views[2] = DepthImages[i].View;

    //     GPassFramebuffers[i].Create(temp_views, *pipeline, Extent);
    // }

    // Log::Debug("Create GPass framebuffers", 0);

    // assert(OutputImages.Size == ColorImages.Size);

    CompFramebuffers.Free();
    CompFramebuffers.InitSize(OutputImages.Size);

    FxSizedArray<VkImageView> temp_views2;
    temp_views2.InitSize(1);

    for (int i = 0; i < OutputImages.Size; i++) {
        temp_views2[0] = OutputImages[i].View;

        CompFramebuffers[i].Create(temp_views2, *comp_pipeline, Extent);
    }

    ColorSampler.Create();
    PositionSampler.Create();

    // mPipeline = pipeline;
    mCompPipeline = comp_pipeline;


}

void RvkSwapchain::DestroyFramebuffersAndImageViews()
{
    for (int i = 0; i < RendererFramesInFlight; i++) {
        // GPassFramebuffers[i].Destroy();
        CompFramebuffers[i].Destroy();

        // ColorImages[i].Destroy();
        // DepthImages[i].Destroy();
        // PositionImages[i].Destroy();


        // vkDestroyImageView(mDevice->Device, OutputImages[i].View, nullptr);
        OutputImages[i].Image = nullptr;
        OutputImages[i].Destroy();
    }

    ColorSampler.Destroy();
    PositionSampler.Destroy();

    CompFramebuffers.Free();
    // GPassFramebuffers.Free();

    // ColorImages.Free();
    // DepthImages.Free();
    // PositionImages.Free();
}

void RvkSwapchain::DestroyInternalSwapchain()
{
    vkDestroySwapchainKHR(mDevice->Device, mSwapchain, nullptr);
}

void RvkSwapchain::Destroy()
{
    if (!Initialized) {
        return;
    }

    DestroyFramebuffersAndImageViews();
    // Images.Free();
    DestroyInternalSwapchain();

    Initialized = false;
}

RvkSwapchain::~RvkSwapchain()
{
    Destroy();
    // Images.Free();
}
