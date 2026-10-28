#include "Image.hpp"

#include "BarrierHelper.hpp"

#include <Asset/Loader/Image/LoaderStb.hpp>
#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <Core/File.hpp>
#include <Core/Allocator.hpp>
#include <Core/StackArray.hpp>
#include <Engine.hpp>
#include <Math/Float16.hpp>
#include <Renderer/Backend/Fwd/Fwd_GetFrame.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Texture/TextureManager.hpp>

#include <cmath>
#include <limits>

namespace fx {

FX_SET_MODULE_NAME("Image")

const ImageTypeProperties ImageTypeGetProperties(eImageType image_type, uint32 cube_count)
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
	else if (image_type == eImageType::CubemapArray) {
		props.ViewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
		props.LayerCount = 6 * std::max(cube_count, 1U);
	}
	else if (image_type == eImageType::Volume) {
		props.ViewType = VK_IMAGE_VIEW_TYPE_3D;
		props.LayerCount = 1;
	}
	else {
		LogError("Unknown image type!");
	}

	return props;
}

static Vec2u GetMipDimensions(const Vec2u& ml_zero_size, uint32 mip_level)
{
	float32 mip_divisor = (1.0f / static_cast<float32>(1U << mip_level));

	return Vec2u(std::max(static_cast<uint32>(static_cast<float32>(ml_zero_size.X) * mip_divisor), 1U),
				 std::max(static_cast<uint32>(static_cast<float32>(ml_zero_size.Y) * mip_divisor), 1U));
}

Image::Image() { mpRefCnt = StdAllocator::Alloc<RefCount>(sizeof(RefCount)); }

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

	Aspect = other.Aspect;

	InternalImage = other.InternalImage;
	View = other.View;

	ImageLayout = other.ImageLayout;
	Allocation = other.Allocation;

	Info = other.Info;

	// Set the new ref count
	mpRefCnt = other.mpRefCnt;

	if (!mpRefCnt) {
		mpRefCnt = StdAllocator::Alloc<RefCount>(sizeof(RefCount));
	}
	else {
		mpRefCnt->Inc();
	}

	return *this;
}

void Image::Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format,
				   VkImageTiling tiling, VkImageUsageFlags usage, eImageAspectFlag aspect, eImageCreateFlags flags,
				   uint32 cube_count, uint32 depth)
{
	using namespace renderer;

	GpuDevice* device = gGraphics->GetDevice();

	Assert(size.X > 0 && size.Y > 0);

	// Destroy image if it already has been created
	if (InternalImage != nullptr && Allocation != nullptr) {
		if (View != nullptr) {
			vkDestroyImageView(device->Device, View, nullptr);
		}

		vmaDestroyImage(gGraphics->GpuAllocator, InternalImage, this->Allocation);

		InternalImage = nullptr;
		Allocation = nullptr;
		View = nullptr;
	}

	Aspect = aspect;
	Info = ImageInfo { size, format, 0, mips_count, Slice<const uint8>(nullptr, 0) };
	Info.ImageType = image_type;
	Info.Depth = std::max(depth, 1U);

	if (!mpRefCnt) {
		mpRefCnt = StdAllocator::Alloc<RefCount>(sizeof(RefCount));
	}

	// Get the vulkan values for the image type
	ImageTypeProperties image_type_props = ImageTypeGetProperties(image_type, cube_count);

	VkImageCreateFlags image_create_flags = 0;

	if (image_type == eImageType::Cubemap || image_type == eImageType::CubemapArray) {
		image_create_flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}

	uint32 queue_families[] = { device->mQueueFamilies.GetGraphicsFamily(),
								device->mQueueFamilies.GetTransferFamily() };

	VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
	uint32 num_queue_families = 0;

	const bool has_independent_xfer = gGraphics->GetDevice()->mQueueFamilies.HasIndependentTransfer();

	if (!HasFlag(flags, eImageCreateFlags::IsTarget) && has_independent_xfer) {
		sharing_mode = VK_SHARING_MODE_CONCURRENT;
		num_queue_families = std::size(queue_families);
	}

	// Create the vulkan image
	VkImageType vk_image_type = VK_IMAGE_TYPE_2D;

	if (image_type == eImageType::Volume) {
		vk_image_type = VK_IMAGE_TYPE_3D;
	}

	VkImageCreateInfo image_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = image_create_flags,

		.imageType = vk_image_type,
		.format = ImageFormatUtil::ToUnderlying(format),
		.extent = { .width = size.Width(), .height = size.Height(), .depth = Info.Depth },

		.mipLevels = mips_count,
		.arrayLayers = image_type_props.LayerCount,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,

		.usage = usage,
		.sharingMode = sharing_mode,

		.queueFamilyIndexCount = num_queue_families,
		.pQueueFamilyIndices = queue_families,

		.initialLayout = ImageLayout,
	};

	VmaAllocationCreateInfo create_info {
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.priority = 1.0f,
	};

	VkResult status = vmaCreateImage(gGraphics->GpuAllocator, &image_info, &create_info, &InternalImage, &Allocation,
									 nullptr);
	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Could not create vulkan image", status);
	}

	static uint32 alloc_number = 0;

	std::string alloc_name = std::to_string(alloc_number++);
	vmaSetAllocationName(gGraphics->GpuAllocator, Allocation, alloc_name.c_str());

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
				.levelCount = mips_count,
				.baseArrayLayer = 0,
				.layerCount = image_type_props.LayerCount,
			},
	};

	status = vkCreateImageView(device->Device, &view_create_info, nullptr, &View);
	if (status != VK_SUCCESS) {
		ModulePanicVulkan("Could not create swapchain image view", status);
	}

#ifdef FX_DEBUG_IMAGE_VIEWS
	std::string image_view_name = "";
	{
		static int view_allocation_number = 0;
		view_allocation_number++;

		char name_buffer[256];

		snprintf(name_buffer, 128, "View(%u,%u){%u}", size.Width(), size.Height(), view_allocation_number);

		Util::SetDebugLabel(name_buffer, VK_OBJECT_TYPE_IMAGE_VIEW, View);
	}
#endif
}

void Image::Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format,
				   VkImageUsageFlags usage, eImageAspectFlag aspect, eImageCreateFlags flags, uint32 cube_count)
{
	Create(image_type, size, mips_count, format, VK_IMAGE_TILING_OPTIMAL, usage, aspect, flags, cube_count);
}

void Image::CreateVolume(const Vec2u& size, uint32 depth, eImageFormat format, VkImageUsageFlags usage)
{
	Create(eImageType::Volume, size, 1, format, VK_IMAGE_TILING_OPTIMAL, usage, eImageAspectFlag::Color,
		   eImageCreateFlags::IsTarget, 1, depth);
}


void Image::CreateFromData(renderer::CommandBuffer& cmd, const ImageInfo& info, eImageCreateFlags flags)
{
	// Upload image to staging buffer
	renderer::RawGpuBuffer staging_buffer;
	staging_buffer.Create(renderer::eGpuBufferType::Transfer, info.ImageData.Size, VMA_MEMORY_USAGE_CPU_TO_GPU,
						  eGpuBufferFlags::TransferReceiver);
	staging_buffer.Upload(info.ImageData);

	const VkImageUsageFlags usage_flags = (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
										   ImageFormatUtil::GetFormatUsageFlags(info.Format) |
										   VK_IMAGE_USAGE_SAMPLED_BIT);


	Create(info.ImageType, info.Size, info.MipCount, info.Format, VK_IMAGE_TILING_OPTIMAL, usage_flags,
		   ImageFormatUtil::GetAspectFlag(info.Format), flags);

	CopyFromBuffer(cmd, staging_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				   GetMipDimensions(info.Size, info.MipLevel), 0, info.MipLevel);

	Info.MipLevel = info.MipLevel;
}

void Image::UploadMip(renderer::CommandBuffer& cmd, uint32 mip_index, const Vec2u& size,
					  const Slice<const uint8>& image_data)
{
	renderer::RawGpuBuffer staging_buffer;
	staging_buffer.Create(renderer::eGpuBufferType::Transfer, image_data.Size, VMA_MEMORY_USAGE_CPU_TO_GPU,
						  eGpuBufferFlags::TransferReceiver);
	staging_buffer.Upload(image_data);

	const VkImageUsageFlags usage_flags = (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
										   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	Info.MipLevel = std::min(Info.MipLevel, mip_index);

	CopyToMip(cmd, staging_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetMipDimensions(size, mip_index),
			  mip_index);
}


void Image::Upload(renderer::CommandBuffer& cmd, const ImageInfo& info)
{
	const Slice<const uint8>& image_data = info.ImageData;

	renderer::RawGpuBuffer staging_buffer;
	staging_buffer.Create(renderer::eGpuBufferType::Transfer, image_data.Size, VMA_MEMORY_USAGE_CPU_TO_GPU,
						  eGpuBufferFlags::TransferReceiver);
	staging_buffer.Upload(image_data);

	const VkImageUsageFlags usage_flags = (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
										   ImageFormatUtil::GetFormatUsageFlags(info.Format) |
										   VK_IMAGE_USAGE_SAMPLED_BIT);

	const eImageAspectFlag aspect_flag = ImageFormatUtil::GetAspectFlag(info.Format);

	Create(info.ImageType, info.Size, info.MipCount, info.Format, VK_IMAGE_TILING_OPTIMAL, usage_flags, aspect_flag);

	SizedArray<VkBufferImageCopy> buffer_copy_infos(info.MipCount);

	uint64 offset = 0;

	for (uint32 info_index = 0; info_index < info.MipCount; info_index++) {
		Vec2u mip_dimensions = GetMipDimensions(info.Size, info_index);
		Assert(mip_dimensions.X > 0 && mip_dimensions.Y > 0);

		AssertMsg((offset % 4) == 0, "Mip level is not 4 byte aligned in the staging buffer");

		buffer_copy_infos.Insert(VkBufferImageCopy {
			.bufferOffset = offset,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource {
				.aspectMask = static_cast<const VkImageAspectFlags>(aspect_flag),
				.mipLevel = info_index,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageExtent =
				VkExtent3D {
					.width = mip_dimensions.X,
					.height = mip_dimensions.Y,
					.depth = 1,
				},
		});

		offset += ImageFormatUtil::GetDataSize(info.Format, mip_dimensions);
	}


	// Transition all levels to transfer dest
	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, 0, info.MipCount);

	vkCmdCopyBufferToImage(cmd, staging_buffer.Buffer, InternalImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						   buffer_copy_infos.Size, buffer_copy_infos.pData);

	// Transition to shader r/o
	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0,
												   info.MipCount);

	// Matches CreateFromData(), the most detailed level this image actually holds
	Info.MipLevel = info.MipLevel;
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

	if (mip_level < Info.MipLevel) {
		Info.MipLevel = mip_level;
	}


	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, mip_level, 1);

	VkBufferImageCopy copy {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource {
			.aspectMask = ImageFormatUtil::GetAspectMask(Info.Format),
			.mipLevel = mip_level,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.imageOffset =
			VkOffset3D {
				.x = static_cast<int32>(dst_offset.X),
				.y = static_cast<int32>(dst_offset.Y),
				.z = 0,
			},
		.imageExtent =
			VkExtent3D {
				.width = size.X,
				.height = size.Y,
				.depth = 1,
			},
	};

	vkCmdCopyBufferToImage(cmd, buffer.Buffer, InternalImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, mip_level, 1);
}

void Image::CopyToMip(renderer::CommandBuffer& cmd, const renderer::RawGpuBuffer& buffer, VkImageLayout final_layout,
					  Vec2u size, uint32 mip_level)
{
	renderer::BarrierHelper::ImageLayoutTransition(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, mip_level, 1);

	VkBufferImageCopy copy {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = mip_level,
			.baseArrayLayer = 0,
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

void Image::DecRef()
{
	if (!mpRefCnt) {
		return;
	}

	if (mpRefCnt->Dec() > 0) {
		return;
	}

	StdAllocator::Free(mpRefCnt);
	mpRefCnt = nullptr;

	if (View != nullptr) {
		vkDestroyImageView(renderer::gGraphics->GetDevice()->Device, View, nullptr);
	}

	if (InternalImage != nullptr && Allocation != nullptr) {
		vmaDestroyImage(renderer::gGraphics->GpuAllocator, InternalImage, this->Allocation);
	}

	InternalImage = nullptr;
	Allocation = nullptr;
	View = nullptr;
}



static uint8 UnitToByte(float32 value)
{
	if (!(value > 0.0f)) {
		return 0;
	}
	if (value >= 1.0f) {
		return 255;
	}
	return static_cast<uint8>(value * 255.0f + 0.5f);
}

static uint8 LinearToSrgbByte(float32 value)
{
	if (!(value > 0.0f)) {
		return 0;
	}
	if (value <= 0.0031308f) {
		return UnitToByte(value * 12.92f);
	}
	return UnitToByte(1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f);
}

static bool ConvertToRgba8(eImageFormat format, const uint8* src, uint32 pixel_count, uint8* dst)
{
	for (uint32 i = 0; i < pixel_count; i++) {
		uint8* out = dst + i * 4;

		switch (format) {
		case eImageFormat::RGBA8_UNorm:
		case eImageFormat::RGBA8_SRGB:
			memcpy(out, src + i * 4, 4);
			break;

		case eImageFormat::BGRA8_UNorm:
		case eImageFormat::BGRA8_SRGB:
			out[0] = src[i * 4 + 2];
			out[1] = src[i * 4 + 1];
			out[2] = src[i * 4 + 0];
			out[3] = src[i * 4 + 3];
			break;

		case eImageFormat::RGBA16_Float: {
			uint16 texel[4];
			memcpy(texel, src + i * 8, sizeof(texel));
			out[0] = LinearToSrgbByte(Float16::FromBits(texel[0]).ToFloat());
			out[1] = LinearToSrgbByte(Float16::FromBits(texel[1]).ToFloat());
			out[2] = LinearToSrgbByte(Float16::FromBits(texel[2]).ToFloat());
			out[3] = UnitToByte(Float16::FromBits(texel[3]).ToFloat());
			break;
		}

		case eImageFormat::RG16_UNorm: {
			uint16 texel[2];
			memcpy(texel, src + i * 4, sizeof(texel));
			out[0] = static_cast<uint8>(texel[0] >> 8);
			out[1] = static_cast<uint8>(texel[1] >> 8);
			out[2] = 0;
			out[3] = 255;
			break;
		}

		case eImageFormat::R32_SFloat:
		case eImageFormat::D32_Float: {
			float32 texel;
			memcpy(&texel, src + i * 4, sizeof(texel));
			const uint8 grey = UnitToByte(texel);
			out[0] = grey;
			out[1] = grey;
			out[2] = grey;
			out[3] = 255;
			break;
		}

		case eImageFormat::R8_UNorm:
			out[0] = src[i];
			out[1] = src[i];
			out[2] = src[i];
			out[3] = 255;
			break;

		default:
			return false;
		}
	}

	return true;
}

void Image::SaveToFile(const String& path, eImageSaveFormat file_format)
{
	if (InternalImage == nullptr) {
		LogError(LC_RENDER, "Cannot save image to '{}': image is not created", path.CStr());
		return;
	}

	if (ImageFormatUtil::IsStencil(Info.Format)) {
		LogError(LC_RENDER, "Cannot save image to '{}': depth/stencil formats are not supported", path.CStr());
		return;
	}

	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	if (ImageFormatUtil::IsDepth(Info.Format)) {
		aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	const uint32 mip_level = Info.MipLevel;
	const Vec2u size = GetMipDimensions(Info.Size, mip_level);
	const uint64 data_size = ImageFormatUtil::GetDataSize(Info.Format, size);

	VkImageLayout original_layout = ImageLayout;
	if (original_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		LogWarning(LC_RENDER, "Image saved to '{}' has no tracked layout, assuming SHADER_READ_ONLY_OPTIMAL",
				   path.CStr());
		original_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	renderer::RawGpuBuffer staging_buffer;
	staging_buffer.Create(renderer::eGpuBufferType::Transfer, data_size, VMA_MEMORY_USAGE_GPU_TO_CPU,
						  eGpuBufferFlags::TransferReceiver);

	const VkImageSubresourceRange subresource_range {
		.aspectMask = aspect,
		.baseMipLevel = mip_level,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};

	renderer::gGraphics->SubmitOneTimeCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			const VkImageMemoryBarrier pre_barrier {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
								 VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout = original_layout,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = InternalImage,
				.subresourceRange = subresource_range,
			};

			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
								 0, nullptr, 1, &pre_barrier);

			const VkBufferImageCopy copy {
				.bufferOffset = 0,
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource {
					.aspectMask = aspect,
					.mipLevel = mip_level,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.imageExtent = VkExtent3D { .width = size.X, .height = size.Y, .depth = 1 },
			};

			vkCmdCopyImageToBuffer(cmd, InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer.Buffer, 1,
								   &copy);

			const VkImageMemoryBarrier post_barrier {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout = original_layout,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = InternalImage,
				.subresourceRange = subresource_range,
			};

			const VkBufferMemoryBarrier host_barrier {
				.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.buffer = staging_buffer.Buffer,
				.offset = 0,
				.size = VK_WHOLE_SIZE,
			};

			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
								 &host_barrier, 1, &post_barrier);
		});

	ImageLayout = original_layout;

	SizedArray<uint8> image_data;
	image_data.InitSize(data_size);

	staging_buffer.Map();
	staging_buffer.InvalidateFromGpu();
	memcpy(image_data.pData, staging_buffer.pMappedBuffer, data_size);
	staging_buffer.UnMap();
	staging_buffer.Destroy();

	if (file_format == eImageSaveFormat::Raw) {
		File file(path, File::eModType::Write, File::eDataType::Binary);
		if (!file.IsFileOpen()) {
			LogError(LC_RENDER, "Could not open '{}' to save raw image data", path.CStr());
			return;
		}
		file.WriteRaw(image_data.pData, data_size);
		return;
	}

	SizedArray<uint8> rgba_data;
	rgba_data.InitSize(uint64(size.X) * size.Y * 4);

	if (!ConvertToRgba8(Info.Format, image_data.pData, size.X * size.Y, rgba_data.pData)) {
		LogError(LC_RENDER, "Cannot save image to '{}': no 8-bit conversion for this format, use eImageSaveFormat::Raw",
				 path.CStr());
		return;
	}

	loader::LoaderStb::SaveToFile(file_format, rgba_data, size, path, eImageSaveFlags::None);
}


Image::~Image() { DecRef(); }


} // namespace fx
