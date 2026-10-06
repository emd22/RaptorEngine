#include "Image.hpp"

#include "BarrierHelper.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ImageFile.hpp>
#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <Core/File.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/StackArray.hpp>
#include <Engine.hpp>
#include <Renderer/Backend/Fwd/Fwd_GetFrame.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Texture/TextureManager.hpp>

namespace fx {

FX_SET_MODULE_NAME("Image")

const ImageTypeProperties ImageTypeGetProperties(eImageType image_type, uint32 cube_count)
{
	ImageTypeProperties props {};

	int32 view_type = 0;

	if (rx_image_type_properties(static_cast<uint32>(image_type), cube_count, &view_type, &props.LayerCount) == 0) {
		LogError("Unknown image type!");
		return props;
	}

	props.ViewType = static_cast<VkImageViewType>(view_type);

	return props;
}

static Vec2u GetMipDimensions(const Vec2u& ml_zero_size, uint32 mip_level)
{
	Vec2u dimensions;

	rx_image_mip_dimensions(ml_zero_size.X, ml_zero_size.Y, mip_level, &dimensions.X, &dimensions.Y);

	return dimensions;
}

Image::Image() { mpRecord = rx_image_new(); }

Image::Image(const RxImage* record)
{
	mpRecord = const_cast<RxImage*>(record);
	rx_image_retain(mpRecord);
}

Image::Image(const Image& other) { ShareOrCopy(other); }

Image::Image(Image&& other) noexcept : ID(other.ID), mpRecord(other.mpRecord)
{
	other.mpRecord = nullptr;
}

Image& Image::operator=(const Image& other)
{
	if (mpRecord == other.mpRecord) {
		return *this;
	}

	Release();

	ShareOrCopy(other);

	return *this;
}

void Image::ShareOrCopy(const Image& other)
{
	// Copying an image that has not been created gives a description of it, which is then created on its own.
	if (other.mpRecord->image == 0 && other.mpRecord->view == 0) {
		mpRecord = rx_image_new();
		*mpRecord = *other.mpRecord;
		return;
	}

	mpRecord = other.mpRecord;
	rx_image_retain(mpRecord);
}

Image& Image::operator=(Image&& other) noexcept
{
	if (this == &other) {
		return *this;
	}

	Release();

	ID = other.ID;
	mpRecord = other.mpRecord;
	other.mpRecord = nullptr;

	return *this;
}

ImageInfo Image::GetInfo() const
{
	ImageInfo info;

	info.Size = GetSize();
	info.ImageType = static_cast<eImageType>(mpRecord->image_type);
	info.Format = GetFormat();
	info.MipLevel = mpRecord->mip_level;
	info.MipCount = mpRecord->mip_count;

	return info;
}

void Image::SetInfo(Vec2u size, eImageFormat format, uint32 mip_level, uint32 mip_count)
{
	SetSize(size);

	mpRecord->image_type = static_cast<uint16>(eImageType::Flat);
	mpRecord->format = static_cast<uint16>(format);
	mpRecord->mip_level = mip_level;
	mpRecord->mip_count = mip_count;
}

void Image::WrapExternal(VkImage image, Vec2u size, eImageFormat format)
{
	rx_image_wrap_external(mpRecord, renderer::RxRaw(image), size.X, size.Y, static_cast<uint16>(format));
}

void Image::RecreateColorView()
{
	const VkResult status = static_cast<VkResult>(
		rx_image_recreate_color_view(mpRecord, renderer::gGraphics->GetDevice()->GetRustDevice()));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Could not create image view", status);
	}
}

void Image::Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format,
				   VkImageTiling tiling, VkImageUsageFlags usage, eImageAspectFlag aspect, eImageCreateFlags flags,
				   uint32 cube_count)
{
	using namespace renderer;

	GpuDevice* device = gGraphics->GetDevice();

	Assert(size.X > 0 && size.Y > 0);

	const RxImageDesc desc = {
		.image_type = static_cast<uint32>(image_type),
		.width = size.Width(),
		.height = size.Height(),
		.mips = mips_count,
		.format = static_cast<uint32>(format),
		.tiling = static_cast<int32>(tiling),
		.usage = usage,
		.aspect = static_cast<uint32>(aspect),
		.cube_count = cube_count,
		.initial_layout = static_cast<int32>(VK_IMAGE_LAYOUT_UNDEFINED),
		.is_target = HasFlag(flags, eImageCreateFlags::IsTarget),
	};

	const VkResult status =
		static_cast<VkResult>(rx_image_create(mpRecord, device->GetRustDevice(), gGraphics->GpuAllocator, &desc));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Could not create vulkan image", status);
	}

#ifdef FX_DEBUG_IMAGE_VIEWS
	std::string image_view_name = "";
	{
		static int view_allocation_number = 0;
		view_allocation_number++;

		char name_buffer[256];

		snprintf(name_buffer, 128, "View(%u,%u){%u}", size.Width(), size.Height(), view_allocation_number);

		Util::SetDebugLabel(name_buffer, VK_OBJECT_TYPE_IMAGE_VIEW, GetView());
	}
#endif
}

void Image::Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format,
				   VkImageUsageFlags usage, eImageAspectFlag aspect, eImageCreateFlags flags, uint32 cube_count)
{
	Create(image_type, size, mips_count, format, VK_IMAGE_TILING_OPTIMAL, usage, aspect, flags, cube_count);
}


namespace {

void DeleteStagingBuffer(RxBufferResource* resource) { gAssetManager->DeleteBuffer(resource); }

RxUploadInfo ToRust(const ImageInfo& info)
{
	return RxUploadInfo { .image_type = static_cast<uint32>(info.ImageType),
						  .width = info.Size.X,
						  .height = info.Size.Y,
						  .format = static_cast<uint16>(info.Format),
						  .mip_level = info.MipLevel,
						  .mip_count = info.MipCount,
						  .data = info.ImageData.pData,
						  .size = info.ImageData.Size };
}

} // namespace

void Image::CreateFromData(renderer::CommandBuffer& cmd, const ImageInfo& info, eImageCreateFlags flags)
{
	const RxUploadInfo upload = ToRust(info);

	const VkResult status = static_cast<VkResult>(rx_image_create_from_data(
		mpRecord, renderer::gGraphics->GetDevice()->GetRustDevice(), renderer::gGraphics->GpuAllocator, cmd.Get(),
		cmd.QueueFamily(), &DeleteStagingBuffer, &upload, HasFlag(flags, eImageCreateFlags::IsTarget)));

	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Could not create vulkan image from data", status);
	}
}

void Image::UploadMip(renderer::CommandBuffer& cmd, uint32 mip_index, const Vec2u& size,
					  const Slice<const uint8>& image_data)
{
	renderer::RawGpuBuffer staging_buffer;
	staging_buffer.Create(renderer::eGpuBufferType::Transfer, image_data.Size, RX_MEMORY_CPU_TO_GPU,
						  eGpuBufferFlags::TransferReceiver);
	staging_buffer.Upload(image_data);

	const VkImageUsageFlags usage_flags = (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
										   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	SetMipLevel(std::min(GetMipLevel(), mip_index));

	CopyToMip(cmd, staging_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetMipDimensions(size, mip_index),
			  mip_index);
}


void Image::Upload(renderer::CommandBuffer& cmd, const ImageInfo& info)
{
	const RxUploadInfo upload = ToRust(info);

	const int32 status = rx_image_upload_chain(mpRecord, renderer::gGraphics->GetDevice()->GetRustDevice(),
											   renderer::gGraphics->GpuAllocator, cmd.Get(), cmd.QueueFamily(),
											   &DeleteStagingBuffer, &upload);

	AssertMsg(status == 0, "Could not upload the mip chain (a mip level is not 4 byte aligned in the chain?)");
}


void Image::MarkUploaded() { renderer::gGraphics->GetFrameNumber(); }

struct LayoutTransitionInfo
{
	VkAccessFlags AccessMask = VK_ACCESS_NONE;
	VkPipelineStageFlags StageMask = VK_PIPELINE_STAGE_NONE;
};


static const LayoutTransitionInfo GetLayoutTransitionInfo(VkImageLayout layout)
{
	switch (layout) {
	case VK_IMAGE_LAYOUT_UNDEFINED:
		return { 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return { VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		return { VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };

		/////////////////////////////////////
		// Input Attachments
		/////////////////////////////////////

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		return { VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
		return { VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
				 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };


		/////////////////////////////////////
		// Output Targets
		/////////////////////////////////////

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return { VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		return { VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };

	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		return { 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };

	default:;
		LogError("Unknown image layout!");
		FX_BREAKPOINT;
	}

	return {};
}


void Image::CopyFromBuffer(renderer::CommandBuffer& cmd, const renderer::RawGpuBuffer& buffer,
						   VkImageLayout final_layout, Vec2u size, uint32 base_layer, uint32 mip_level,
						   Vec2u dst_offset)
{
	if (mip_level < 0) {
		return;
	}

	if (mip_level < GetMipLevel()) {
		SetMipLevel(mip_level);
	}


	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, mip_level, 1);

	rx_gpu_cmd_copy_buffer_to_region(renderer::gGraphics->GetDevice()->GetRustDevice(), cmd.Get(),
									 buffer.GetRaw(), GetRawImage(), mip_level, size.X,
									 size.Y, static_cast<int32>(dst_offset.X), static_cast<int32>(dst_offset.Y));

	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, mip_level, 1);
}

void Image::CopyToMip(renderer::CommandBuffer& cmd, const renderer::RawGpuBuffer& buffer, VkImageLayout final_layout,
					  Vec2u size, uint32 mip_level)
{
	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, mip_level, 1);

	rx_gpu_cmd_copy_buffer_to_region(renderer::gGraphics->GetDevice()->GetRustDevice(), cmd.Get(),
									 buffer.GetRaw(), GetRawImage(), mip_level, size.X,
									 size.Y, 0, 0);

	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, mip_level, 1);
}

enum eCubemapLayer
{
	Right,
	Left,
	Top,
	Bottom,
	Front,
	Back,
};

// void Image::CreateLayeredImageFromCubemap(Image& cubemap, eImageFormat image_format, VkImageAspectFlags aspect_flags,
// 										  ImageCubemapOptions options)
// {
// 	// Here is the type of cubemap we will be reading here:
// 	//
// 	// T -> Top, B -> Bottom, L -> Left, R -> Right
// 	// FW -> Forward, BW -> Backward
// 	//
// 	// +-----+-----+-----+-----+
// 	// |     |  T  |     |     |
// 	// +-----+-----+-----+-----+
// 	// |  L  |  FW |  R  |  BW |
// 	// +-----+-----+-----+-----+
// 	// |     |  B  |     |     |
// 	// +-----+-----+-----+-----+
// 	//
// 	// Note that it is 4 tiles wide and 3 tiles tall.


// 	const uint32 tile_width = cubemap.Info.Size.X / 4;
// 	const uint32 tile_height = cubemap.Info.Size.Y / 3;

// 	Assert(tile_width == tile_height);


// 	Create(eImageType::Cubemap, Vec2u(tile_width, tile_height), 1, image_format, VK_IMAGE_TILING_OPTIMAL,
// 		   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, Aspect);

// 	StackArray<VkImageCopy, 6> copy_infos;

// 	VkImageCopy copy_info {
// 		.srcSubresource = { .aspectMask = aspect_flags, .baseArrayLayer = 0, .layerCount = 1, },
// 		.dstSubresource = { .aspectMask = aspect_flags, .baseArrayLayer = 0, .layerCount = 1, },
// 		.dstOffset = { .x = 0, .y = 0 },
// 		.extent = { .width = tile_width, .height = tile_height, .depth = 1 },
// 	};

// 	// Top
// 	{
// 		copy_info.srcOffset = { .x = static_cast<int32>(tile_width), .y = 0 };
// 		copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Top;

// 		copy_infos.Insert(copy_info);
// 	}


// 	// Left
// 	{
// 		copy_info.srcOffset = { .x = 0, .y = static_cast<int32>(tile_height) };
// 		copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Left;


// 		copy_infos.Insert(copy_info);
// 	}

// 	// Front

// 	{
// 		copy_info.srcOffset = { .x = static_cast<int32>(tile_width), .y = static_cast<int32>(tile_height) };
// 		copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Front;


// 		copy_infos.Insert(copy_info);
// 	}

// 	// Forward
// 	{
// 		copy_info.srcOffset = { .x = static_cast<int32>(tile_width) * 2, .y = static_cast<int32>(tile_height) };
// 		copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Right;

// 		copy_infos.Insert(copy_info);
// 	}

// 	// Back
// 	{
// 		copy_info.srcOffset = { .x = static_cast<int32>(tile_width) * 3, .y = static_cast<int32>(tile_height) };
// 		copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Back;

// 		copy_infos.Insert(copy_info);
// 	}

// 	// Bottom
// 	{
// 		copy_info.srcOffset = { .x = static_cast<int32>(tile_width), .y = static_cast<int32>(tile_height) * 2 };
// 		copy_info.dstSubresource.baseArrayLayer = eCubemapLayer::Bottom;

// 		copy_infos.Insert(copy_info);
// 	}


// 	renderer::gRenderer->SubmitOneTimeCmd(
// 		[&](renderer::CommandBuffer& cmd)
// 		{
// 			cubemap.TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd, 1);
// 			TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, 6);

// 			vkCmdCopyImage(cmd.Get(), cubemap.InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, InternalImage,
// 						   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, copy_infos.pData);

// 			TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 6);
// 		});
// }

void Image::Release()
{
	if (mpRecord == nullptr) {
		return;
	}

	renderer::GraphicsBackend* graphics = renderer::gGraphics;

	if (graphics != nullptr) {
		rx_image_release(mpRecord, graphics->GetDevice()->GetRustDevice(), graphics->GpuAllocator);
	}
	else {
		rx_image_release(mpRecord, nullptr, nullptr);
	}

	mpRecord = nullptr;
}


void Image::SaveToFile(const String& path, eImageSaveFormat file_format)
{
	const Vec2u image_size = GetSize();
	const uint32 data_size = image_size.X * image_size.Y * ImageFormatUtil::GetPixelStride(GetFormat());

	SizedArray<uint8> image_data;
	image_data.InitSize(data_size);

	renderer::gGraphics->SubmitOneTimeCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			renderer::RawGpuBuffer staging_buffer;
			staging_buffer.Create(renderer::eGpuBufferType::Transfer, data_size, RX_MEMORY_GPU_TO_CPU,
								  eGpuBufferFlags::TransferReceiver);

			VkImageMemoryBarrier pre_barrier {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout = GetLayout(),
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = Get(),
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
				.imageExtent = VkExtent3D { .width = image_size.X, .height = image_size.Y, .depth = 1 },
			};

			vkCmdCopyImageToBuffer(cmd, Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer.Get(), 1,
								   &copy);

			VkImageMemoryBarrier post_barrier {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = Get(),
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

			SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			staging_buffer.Map();
			memcpy(image_data.pData, staging_buffer.GetMapped(), data_size);
			staging_buffer.UnMap();
			staging_buffer.Destroy();
		});


	SaveImageFile(file_format, image_data, image_size, path);
}


Image::~Image() { Release(); }


} // namespace fx
