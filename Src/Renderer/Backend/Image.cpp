
#include "Image.hpp"

#include <Asset/Loader/AxLoaderStb.hpp>
#include <Core/Defines.hpp>
#include <Core/File.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/Panic.hpp>
#include <Core/StackArray.hpp>
#include <Engine.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("Image")

const ImageTypeProperties ImageTypeGetProperties(eImageType image_type)
{
    ImageTypeProperties props {};

    if (image_type == eImageType::Flat) {
        props.ViewType = VK_IMAGE_VIEW_TYPE_2D;
        props.LayerCount = 1;
    }
    else if (image_type == eImageType::Cubemap) {
        props.ViewType = VK_IMAGE_VIEW_TYPE_CUBE;
        props.LayerCount = 6;
    }
    else {
        LogError("Unknown image type!");
    }

    return props;
}

Image::Image() { mpRefCnt = gEnginePool->Alloc<RefCount>(sizeof(RefCount)); }

Image::Image(const Image& other) { (*this) = other; }

Image::Image(Ref<Image>&& ref) { (*this) = std::move(ref); }

Image& Image::operator=(Ref<Image>&& ref)
{
    // Set the image properties, move the ref count over.
    (*this) = *ref;

    mpRefCnt = ref.mpRefCnt;
    ref.mpRefCnt = nullptr;

    ref.DestroyRef();

    return *this;
}


Image& Image::operator=(const Image& other)
{
    // If this image is already established, then we should decrement the ref here.
    if (mpRefCnt) {
        DecRef();
    }

    Size = other.Size;
    Aspect = other.Aspect;

    InternalImage = other.InternalImage;
    View = other.View;

    ViewType = other.ViewType;
    Format = other.Format;
    ImageLayout = other.ImageLayout;
    Allocation = other.Allocation;

    // Set the new ref count
    mpRefCnt = other.mpRefCnt;

    if (!mpRefCnt) {
        mpRefCnt = gEnginePool->Alloc<RefCount>(sizeof(RefCount));
    }
    else {
        mpRefCnt->Inc();
    }

    return *this;
}

void Image::Create(eImageType image_type, const Vec2u& size, eImageFormat format, VkImageTiling tiling,
                   VkImageUsageFlags usage, eImageAspectFlag aspect)
{
    Assert(size.X > 0 && size.Y > 0);

    // Destroy image if it already has been created
    if (InternalImage != nullptr && Allocation != nullptr) {
        if (View != nullptr) {
            vkDestroyImageView(gRenderer->GetDevice()->Device, View, nullptr);
        }

        vmaDestroyImage(gRenderer->GpuAllocator, InternalImage, this->Allocation);

        InternalImage = nullptr;
        Allocation = nullptr;
        View = nullptr;
    }

    Aspect = aspect;
    Size = size;
    Format = format;
    ViewType = image_type;

    if (!mpRefCnt) {
        mpRefCnt = gEnginePool->Alloc<RefCount>(sizeof(RefCount));
    }

    // Get the vulkan values for the image type
    ImageTypeProperties image_type_props = ImageTypeGetProperties(image_type);

    VkImageCreateFlags image_create_flags = 0;

    if (image_type == eImageType::Cubemap) {
        image_create_flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    // Create the vulkan image
    VkImageCreateInfo image_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = image_create_flags,

        .imageType = VK_IMAGE_TYPE_2D,

        .format = ImageFormatUtil::ToUnderlying(format),

        .extent = { .width = size.Width(), .height = size.Height(), .depth = 1 },

        .mipLevels = 1,
        .arrayLayers = image_type_props.LayerCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,

        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,

        .initialLayout = ImageLayout,

    };

    VmaAllocationCreateInfo create_info {
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .priority = 1.0f,
    };

    VkResult status = vmaCreateImage(gRenderer->GpuAllocator, &image_info, &create_info, &InternalImage, &Allocation,
                                     nullptr);
    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Could not create vulkan image", status);
    }

    static uint32 alloc_number = 0;

    std::string alloc_name = std::to_string(alloc_number++);
    vmaSetAllocationName(gRenderer->GpuAllocator, Allocation, alloc_name.c_str());

    // LogInfo("Create Image (Image={:p}, Allocation={:p})", reinterpret_cast<void*>(Image),
    //           reinterpret_cast<void*>(Allocation));

#ifdef FX_DEBUG_GPU_BUFFER_ALLOCATION_NAMES
    {
        static uint32 allocation_number = 0;
        allocation_number += 1;

        std::string allocation_name = "";
        // typeid(ElementType).name();

        char name_buffer[256];

        snprintf(name_buffer, 128, "Img(%u,%u){%u}", size.Width(), size.Height(), allocation_number);

        vmaSetAllocationName(Fx_Fwd_GetGpuAllocator(), Allocation, name_buffer);
    }
#endif

    const VkImageViewCreateInfo view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = InternalImage,
        .viewType = image_type_props.ViewType,
        .format = ImageFormatUtil::ToUnderlying(format),
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = static_cast<VkImageAspectFlags>(aspect),
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = image_type_props.LayerCount,
            },
    };

    status = vkCreateImageView(gRenderer->GetDevice()->Device, &view_create_info, nullptr, &View);
    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Could not create swapchain image view", status);
    }

    std::string image_view_name = "";

#ifdef FX_DEBUG_IMAGE_VIEWS
    {
        static int view_allocation_number = 0;
        view_allocation_number++;

        char name_buffer[256];

        snprintf(name_buffer, 128, "View(%u,%u){%u}", size.Width(), size.Height(), view_allocation_number);

        Util::SetDebugLabel(name_buffer, VK_OBJECT_TYPE_IMAGE_VIEW, View);
    }
#endif
}

void Image::Create(eImageType image_type, const Vec2u& size, eImageFormat format, VkImageUsageFlags usage,
                   eImageAspectFlag aspect)
{
    Create(image_type, size, format, VK_IMAGE_TILING_OPTIMAL, usage, aspect);
}


void Image::CreateGpuOnly(eImageType image_type, const Vec2u& size, eImageFormat format,
                          const SizedArray<uint8>& image_data)
{
    RawGpuBuffer staging_buffer;
    staging_buffer.Create(eGpuBufferType::Transfer, image_data.Size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                          eGpuBufferFlags::TransferReceiver);
    staging_buffer.Upload(image_data);

    const VkImageUsageFlags usage_flags = (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    Create(image_type, size, format, VK_IMAGE_TILING_OPTIMAL, usage_flags, eImageAspectFlag::Color);

    CopyFromBuffer(staging_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, size);
}

void Image::TransitionDepthToShaderRO(CommandBuffer& cmd)
{
    VkImageMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

        .srcAccessMask = static_cast<VkAccessFlags>(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
        .dstAccessMask = static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT),

        .oldLayout = ImageLayout,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = InternalImage,

        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void Image::TransitionDepthToAttachment(CommandBuffer& cmd)
{
    VkImageMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

        .srcAccessMask = static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT),
        .dstAccessMask = static_cast<VkAccessFlags>(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),

        .oldLayout = ImageLayout,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = InternalImage,

        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    ImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

void Image::TransitionLayout(VkImageLayout new_layout, CommandBuffer& cmd, uint32 layer_count,
                             std::optional<TransitionLayoutOverrides> overrides)
{
    bool is_depth_texture = ImageFormatUtil::IsDepth(Format);

    VkImageAspectFlags aspect_flags = static_cast<VkImageAspectFlags>((is_depth_texture) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                                         : VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

        .srcAccessMask = static_cast<VkAccessFlags>((is_depth_texture) ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
        .dstAccessMask = static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT),

        .oldLayout = ImageLayout,
        .newLayout = new_layout,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = InternalImage,

        .subresourceRange =
            {
                .aspectMask = aspect_flags,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = layer_count,
            },
    };

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkPipelineStageFlags dest_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (ImageLayout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dest_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (ImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    if (is_depth_texture) {
        src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    if (overrides.has_value()) {
        if (overrides->DstStage.has_value()) {
            dest_stage = overrides->DstStage.value();
        }
        if (overrides->DstAccessMask.has_value()) {
            barrier.dstAccessMask = overrides->DstAccessMask.value();
        }
    }

    vkCmdPipelineBarrier(cmd, src_stage, dest_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    ImageLayout = new_layout;
}


void Image::CopyFromBuffer(const RawGpuBuffer& buffer, VkImageLayout final_layout, Vec2u size, uint32 base_layer)
{
    Fx_Fwd_SubmitUploadCmd(
        [&](CommandBuffer& cmd)
        {
            TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, 1,
                             TransitionLayoutOverrides { .DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                         .DstAccessMask = VK_ACCESS_TRANSFER_READ_BIT });

            VkBufferImageCopy copy {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = base_layer,
                    .layerCount = 1,
                },
                .imageExtent =
                    VkExtent3D {
                        .width = size.X,
                        .height = size.Y,
                        .depth = 1,
                    },
            };

            vkCmdCopyBufferToImage(cmd, buffer.Buffer, InternalImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 1,
                             TransitionLayoutOverrides { .DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                         .DstAccessMask = VK_ACCESS_TRANSFER_READ_BIT });
        });
}

enum eCubemapLayer
{
    // Forward,

    Right,
    Left,
    Top,
    Bottom,
    Front,
    Back,
};

void Image::CreateLayeredImageFromCubemap(Image& cubemap, eImageFormat image_format, VkImageAspectFlags aspect_flags,
                                          ImageCubemapOptions options)
{
    // Here is the type of cubemap we will be reading here:
    //
    // T -> Top, B -> Bottom, L -> Left, R -> Right
    // FW -> Forward, BW -> Backward
    //
    // +-----+-----+-----+-----+
    // |     |  T  |     |     |
    // +-----+-----+-----+-----+
    // |  L  |  FW |  R  |  BW |
    // +-----+-----+-----+-----+
    // |     |  B  |     |     |
    // +-----+-----+-----+-----+
    //
    // Note that it is 4 tiles wide and 3 tiles tall.


    const uint32 tile_width = cubemap.Size.X / 4;
    const uint32 tile_height = cubemap.Size.Y / 3;

    Assert(tile_width == tile_height);


    Create(eImageType::Cubemap, Vec2u(tile_width, tile_height), image_format, VK_IMAGE_TILING_OPTIMAL,
           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, Aspect);

    StackArray<VkImageCopy, 6> copy_infos;

    VkImageCopy copy_info {
        .srcSubresource = { .aspectMask = aspect_flags, .baseArrayLayer = 0, .layerCount = 1, },
        .dstSubresource = { .aspectMask = aspect_flags, .baseArrayLayer = 0, .layerCount = 1, },
        .dstOffset = { .x = 0, .y = 0 },
        .extent = { .width = tile_width, .height = tile_height, .depth = 1 },
    };

    // Top
    {
        copy_info.srcOffset = { .x = static_cast<int32>(tile_width), .y = 0 };
        copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Top;

        copy_infos.Insert(copy_info);
    }


    // Left
    {
        copy_info.srcOffset = { .x = 0, .y = static_cast<int32>(tile_height) };
        copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Left;


        copy_infos.Insert(copy_info);
    }

    // Front

    {
        copy_info.srcOffset = { .x = static_cast<int32>(tile_width), .y = static_cast<int32>(tile_height) };
        copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Front;


        copy_infos.Insert(copy_info);
    }

    // Forward
    {
        copy_info.srcOffset = { .x = static_cast<int32>(tile_width) * 2, .y = static_cast<int32>(tile_height) };
        copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Right;

        copy_infos.Insert(copy_info);
    }

    // Back
    {
        copy_info.srcOffset = { .x = static_cast<int32>(tile_width) * 3, .y = static_cast<int32>(tile_height) };
        copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Back;

        copy_infos.Insert(copy_info);
    }

    // Bottom
    {
        copy_info.srcOffset = { .x = static_cast<int32>(tile_width), .y = static_cast<int32>(tile_height) * 2 };
        copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Bottom;

        copy_infos.Insert(copy_info);
    }


    gRenderer->SubmitOneTimeCmd(
        [&](CommandBuffer& cmd)
        {
            cubemap.TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd);
            TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, 6);

            vkCmdCopyImage(cmd.Get(), cubemap.InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, InternalImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, copy_infos.pData);

            TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 6);
        });
}

void Image::DecRef()
{
    if (!mpRefCnt) {
        return;
    }

    if (mpRefCnt->Dec() > 0) {
        return;
    }

    gEnginePool->Free(mpRefCnt);
    mpRefCnt = nullptr;

    if (View != nullptr) {
        vkDestroyImageView(gRenderer->GetDevice()->Device, View, nullptr);
    }

    if (InternalImage != nullptr && Allocation != nullptr) {
        vmaDestroyImage(gRenderer->GpuAllocator, InternalImage, this->Allocation);
    }

    InternalImage = nullptr;
    Allocation = nullptr;
    View = nullptr;
}


// bool Image::Readback(CommandBuffer& cmd)
// {
//     const uint32 data_size = Size.X * Size.Y * ImageFormatUtil::GetSize(Format);

//     RawGpuBuffer staging_buffer;
//     staging_buffer.Create(eGpuBufferType::Transfer, data_size, VMA_MEMORY_USAGE_GPU_TO_CPU, eGpuBufferFlags::None);

//     TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd, 1,
//                      TransitionLayoutOverrides { .DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
//                                                  .DstAccessMask = VK_ACCESS_TRANSFER_READ_BIT });

//     VkBufferImageCopy copy {
//         .bufferOffset = 0,
//         .bufferRowLength = 0,
//         .bufferImageHeight = 0,
//         .imageSubresource {
//             .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
//             .mipLevel = 0,
//             .baseArrayLayer = 0,
//             .layerCount = 1,
//         },
//         .imageExtent = VkExtent3D { .width = Size.X, .height = Size.Y, .depth = 1 },
//     };

//     vkCmdCopyImageToBuffer(cmd, InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer.Buffer, 1,
//     &copy);

//     TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 1,
//                      TransitionLayoutOverrides { .DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
//                                                  .DstAccessMask = VK_ACCESS_SHADER_READ_BIT });

//     return true;
// }


// bool Image::SaveToFile(const String& path, eImageFormat output_format, int jpeg_quality)
// {
//     SizedArray<uint8> data = SaveToMemory(output_format, jpeg_quality);
//     if (data.Size == 0) {
//         return false;
//     }

//     File fp = File(path, File::eModType::Write, File::eDataType::Binary);
//     if (!fp.IsFileOpen()) {
//         LogError("Cannot save image to disk");
//         return false;
//     }

//     fp.WriteRaw(data.pData, data.Size);
//     fp.Close();

//     return true;
// }


void Image::SaveToFile(const String& path, eImageSaveFormat file_format)
{
    const uint32 data_size = Size.X * Size.Y * ImageFormatUtil::GetSize(Format);

    SizedArray<uint8> image_data;
    image_data.InitSize(data_size);

    gRenderer->SubmitOneTimeCmd(
        [&](CommandBuffer& cmd)
        {
            RawGpuBuffer staging_buffer;
            staging_buffer.Create(eGpuBufferType::Transfer, data_size, VMA_MEMORY_USAGE_GPU_TO_CPU,
                                  eGpuBufferFlags::TransferReceiver);

            VkImageMemoryBarrier pre_barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = ImageLayout,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = InternalImage,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                 0, nullptr, 1, &pre_barrier);

            VkBufferImageCopy copy {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageExtent = VkExtent3D { .width = Size.X, .height = Size.Y, .depth = 1 },
            };

            vkCmdCopyImageToBuffer(cmd, InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer.Buffer, 1,
                                   &copy);

            VkImageMemoryBarrier post_barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = InternalImage,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &post_barrier);

            ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            staging_buffer.Map();
            memcpy(image_data.pData, staging_buffer.pMappedBuffer, data_size);
            staging_buffer.UnMap();
            staging_buffer.Destroy();
        });


    AxLoaderStb::SaveToFile(file_format, image_data, Size, path, eImageSaveFlags::None);
}


Image::~Image() { DecRef(); }

} // namespace fx::renderer
