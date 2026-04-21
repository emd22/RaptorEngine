#include "Swapchain.hpp"

#include "Device.hpp"

#include <vulkan/vulkan.h>

#include <Core/Defines.hpp>
#include <Core/Panic.hpp>
#include <Renderer/DeferredRenderer.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("Swapchain")

void Swapchain::Init(Vec2u size, VkSurfaceKHR& surface, GpuDevice* device)
{
    mDevice = device;

    CreateSwapchain(size, surface);
    CreateSamplers();
    CreateSwapchainImages();
    CreateImageViews();
    CreateFramebuffers();

    bInitialized = true;
}

void Swapchain::CreateSwapchainImages()
{
    uint32 image_count;

    vkGetSwapchainImagesKHR(mDevice->Device, mSwapchain, &image_count, nullptr);

    SizedArray<VkImage> raw_images;
    raw_images.InitSize(image_count);

    vkGetSwapchainImagesKHR(mDevice->Device, mSwapchain, &image_count, raw_images.pData);

    OutputImages.InitCapacity(image_count);

    for (VkImage& raw_image : raw_images) {
        Image* image = OutputImages.Insert();
        image->InternalImage = raw_image;
        image->View = nullptr;
        image->Allocation = nullptr;
        image->ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image->Format = Surface.Format;
    }
}

void Swapchain::CreateFramebuffers() {}

void Swapchain::CreateImageViews()
{
    for (int32 i = 0; i < OutputImages.Size; i++) {
        const VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = OutputImages[i].InternalImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ImageFormatUtil::ToUnderlying(Surface.Format),
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
            ModulePanicVulkan("Could not create swapchain image view", status);
        }
    }
}

void Swapchain::CreateSwapchain(Vec2u size, VkSurfaceKHR& surface)
{
    Extent = size;

    VkSurfaceCapabilitiesKHR capabilities;
    const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mDevice->Physical, surface, &capabilities);

    if (result != VK_SUCCESS) {
        ModulePanicVulkan("Error retrieving surface capabilities", result);
    }

    const VkExtent2D extent = {
        .width = size.X,
        .height = size.Y,
    };

    uint32 image_count = capabilities.minImageCount + 1;

    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    LogInfo("Swapchain - Min:{:d}, Max:{:d}, Selected:{:d}", capabilities.minImageCount, capabilities.maxImageCount,
            image_count);

    // Retrieve the image format for the surface (the window's render target)
    {
        VkSurfaceFormatKHR surface_format = mDevice->GetSurfaceFormat();

        // For now we will enforce that RGBA16 is supported by the render device. This is the first chosen
        // if it is supported.
        Assert(surface_format.format == VK_FORMAT_R16G16B16A16_SFLOAT ||
               surface_format.format == VK_FORMAT_R8G8B8A8_UNORM);

        if (surface_format.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            Surface.Format = eImageFormat::RGBA16_Float;
        }
        else if (surface_format.format == VK_FORMAT_R8G8B8A8_UNORM) {
            Surface.Format = eImageFormat::RGBA8_UNorm;
        }

        Surface.ColorSpace = surface_format.colorSpace;
    }

    const VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,

        .minImageCount = image_count,

        .imageFormat = ImageFormatUtil::ToUnderlying(Surface.Format),
        .imageColorSpace = Surface.ColorSpace,

        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,

        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,

        .presentMode = present_mode,
        .clipped = VK_TRUE,
        // mSwapchain is null if not initialized
        .oldSwapchain = mSwapchain,
    };

    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = nullptr;

    const VkResult status = vkCreateSwapchainKHR(mDevice->Device, &create_info, nullptr, &mSwapchain);

    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Could not create swapchain", status);
    }
}

void Swapchain::CreateSamplers()
{
    ColorSampler.Create();

    DepthSampler.Create(SamplerProps {
        eSamplerFilter::Nearest,
        eSamplerFilter::Nearest,
        eSamplerFilter::Nearest,
    });

    ShadowDepthSampler.Create(SamplerProps {
        eSamplerFilter::Linear,
        eSamplerFilter::Linear,
        eSamplerFilter::Linear,
        eSamplerAddressMode::ClampToBorder,
        eSamplerBorderColor::FloatWhite,
        eSamplerCompareOp::Greater,
    });

    NormalsSampler.Create();
    LightsSampler.Create();
}

void Swapchain::DestroyFramebuffersAndImageViews()
{
    for (int i = 0; i < FramesInFlight; i++) {
        // HACK: Clears the image so that we only destroy the image view. This should be updated!
        OutputImages[i].InternalImage = nullptr;
        OutputImages[i].DecRef();
    }


    // TODO: Add sampler cache!
    ColorSampler.Destroy();
    DepthSampler.Destroy();
    ShadowDepthSampler.Destroy();
    NormalsSampler.Destroy();
    LightsSampler.Destroy();
}

void Swapchain::DestroyInternalSwapchain() { vkDestroySwapchainKHR(mDevice->Device, mSwapchain, nullptr); }

void Swapchain::Destroy()
{
    if (!bInitialized) {
        return;
    }

    DestroyFramebuffersAndImageViews();
    // Images.Free();
    DestroyInternalSwapchain();

    bInitialized = false;
}

Swapchain::~Swapchain() { Destroy(); }

} // namespace fx::renderer
