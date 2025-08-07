
#include "RxImage.hpp"

#include <Core/FxPanic.hpp>
#include <Core/FxDefines.hpp>

#include "../Renderer.hpp"
#include "../FxRenderBackend.hpp"

FX_SET_MODULE_NAME("RxImage")

RxGpuDevice* RxImage::GetDevice()
{
    // If the device has not been created with `::Create` then mDevice will not be set.
    // I think this would be fine, there should probably be a `::Build` or `::Create` method that
    // does this instead though.

    if (mDevice == nullptr) {
        mDevice = Renderer->GetDevice();
    }

    return mDevice;
}

void RxImage::Create(
    FxVec2u size,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect_flags
)
{
    Size = size;
    Format = format;
    mDevice = Renderer->GetDevice();

    if (RxUtil::IsFormatDepth(format)) {
        mIsDepthTexture = true;
    }

    // Create the vulkan image
    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,

        // Dimensions
        .extent.width = size.Width(),
        .extent.height = size.Height(),
        .extent.depth = 1,

        .mipLevels = 1,
        .arrayLayers = 1,
        .format = format,
        .tiling = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = usage,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo create_info{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .priority = 1.0f,
    };

    VkResult status = vmaCreateImage(Renderer->GpuAllocator, &image_info, &create_info, &Image, &Allocation, nullptr);
    if (status != VK_SUCCESS) {
        FxModulePanic("Could not create vulkan image", status);
    }

    const VkImageViewCreateInfo view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = Image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = aspect_flags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };

    status = vkCreateImageView(GetDevice()->Device, &view_create_info, nullptr, &View);
    if (status != VK_SUCCESS) {
        FxModulePanic("Could not create swapchain image view", status);
    }
}

void RxImage::TransitionLayout(VkImageLayout new_layout, RxCommandBuffer &cmd)
{
    VkImageAspectFlags depth_bits = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = ImageLayout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = Image,
        .srcAccessMask = (mIsDepthTexture) ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .subresourceRange = {
            .aspectMask = (mIsDepthTexture) ? depth_bits : VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };


    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dest_stage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if (ImageLayout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dest_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (ImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    if (mIsDepthTexture) {
        src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dest_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    ImageLayout = new_layout;
}

void RxImage::Destroy()
{
    if (View != nullptr) {
        vkDestroyImageView(GetDevice()->Device, View, nullptr);
    }

    if (Image != nullptr && Allocation != nullptr) {
        vmaDestroyImage(Renderer->GpuAllocator, this->Image, this->Allocation);
    }

    this->Image = nullptr;
    this->Allocation = nullptr;
    this->View = nullptr;

    // Fx_Fwd_AddToDeletionQueue([this](FxDeletionObject* obj) {
    //     if (this->Image == nullptr || this->Allocation == nullptr) {
    //         return;
    //     }

    //     printf("Destroy image!!\n");

    //     vkDestroyImageView(this->mDevice->Device, this->View, nullptr);
    //     vmaDestroyImage(RendererVulkan->GpuAllocator, this->Image, this->Allocation);

    //     this->Image = nullptr;
    //     this->Allocation = nullptr;
    // });
}
