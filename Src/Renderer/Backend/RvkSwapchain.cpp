#include "RvkSwapchain.hpp"
#include "RvkDevice.hpp"
#include "../Renderer.hpp"

#include <Core/FxDefines.hpp>
#include <Core/FxPanic.hpp>

#include <Renderer/FxDeferred.hpp>

#include <vulkan/vulkan.h>

FX_SET_MODULE_NAME("RvkSwapchain")

void RvkSwapchain::Init(FxVec2u size, VkSurfaceKHR &surface, RvkGpuDevice *device)
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
    }
}

void RvkSwapchain::CreateSwapchain(FxVec2u size, VkSurfaceKHR &surface)
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

void RvkSwapchain::CreateSwapchainFramebuffers()
{
    // CompFramebuffers.Free();
    // CompFramebuffers.InitSize(OutputImages.Size);

    FxSizedArray<VkImageView> temp_views2;
    temp_views2.InitSize(1);

    for (int i = 0; i < OutputImages.Size; i++) {
        temp_views2[0] = OutputImages[i].View;

        // CompFramebuffers[i].Create(temp_views2, *comp_pipeline, Extent);
    }

    ColorSampler.Create();
    DepthSampler.Create(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST);
    NormalsSampler.Create();
    LightsSampler.Create();

    // mCompPipeline = comp_pipeline;


}

void RvkSwapchain::DestroyFramebuffersAndImageViews()
{
    for (int i = 0; i < RendererFramesInFlight; i++) {
        // CompFramebuffers[i].Destroy();

        // Since the RvkImage's `VkImage` is from the swapchain, we do not want to destroy it
        // using VMA. Mark the Image as nullptr incase something happened to the `Allocation` inside.
        OutputImages[i].Image = nullptr;
        OutputImages[i].Destroy();
    }

    ColorSampler.Destroy();
    DepthSampler.Destroy();
    NormalsSampler.Destroy();
    LightsSampler.Destroy();


    // CompFramebuffers.Free();
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
}
