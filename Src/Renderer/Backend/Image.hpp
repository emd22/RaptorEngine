#pragma once

#include <vulkan/vulkan.h>

// #define VMA_DEBUG_LOG(...) OldLog::Warning(__VA_ARGS__)

#include "Commands.hpp"
#include "GpuBuffer.hpp"

#include <ThirdParty/stb_image_write.h>
#include <ThirdParty/vk_mem_alloc.h>

#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Math/Vec2.hpp>
#include <Texture/TextureID.hpp>
#include <optional>

namespace fx {


enum class eSizeDivisor : uint32
{
	FullRes = 1U,
	HalfRes = 2U,
	QuarterRes = 4U,
};


enum class eImageSaveFormat
{
	Jpeg,
	Png,
};

enum class eImageSaveFlags
{
	None = 0,
	FlipY = (1 << 0),
};

FxEnumFlags(eImageSaveFlags);


enum class eImageCreateFlags
{
	None = 0,
	IsTarget = (1 << 0),
};

FxEnumFlags(eImageCreateFlags);


enum class eImageFormat : uint16
{
	None,

	// Color formats

	BGRA8_UNorm,
	RGBA8_SRGB,
	RGBA8_UNorm,

	RG32_Float,

	RGBA16_Float,

	RGB32_Float,

	D16_UNorm_S8_UInt,
	D32_Float,
	D32_Float_S8_UInt,

	R32_SFloat,
	R32_SInt,
	R32_UInt,

	R8_UInt,
	R8_UNorm,
};

enum class eImageType
{
	Flat,
	Cubemap,
};

struct ImageInfo
{
	ImageInfo() = default;

	ImageInfo(Vec2u size, eImageFormat format, int32 mip_level, int32 mip_count, const Slice<const uint8>& data)
		: Size(size), ImageType(eImageType::Flat), Format(format), MipLevel(mip_level), MipCount(mip_count),
		  ImageData(data)
	{
	}

public:
	Vec2u Size = Vec2u::sZero;

	eImageType ImageType = eImageType::Flat;
	eImageFormat Format = eImageFormat::RGBA8_UNorm;
	uint32 MipLevel = 0;
	uint32 MipCount = 1;
	Slice<const uint8> ImageData { nullptr, 0 };
};


struct ImageTypeProperties
{
	VkImageViewType ViewType;
	uint32 LayerCount;
};

enum class eImageAspectFlag
{
	Color = VK_IMAGE_ASPECT_COLOR_BIT,
	Depth = VK_IMAGE_ASPECT_DEPTH_BIT,
};

struct ImageFormatUtil
{
	static constexpr bool IsDepth(eImageFormat format)
	{
		switch (format) {
		case eImageFormat::D16_UNorm_S8_UInt:
		case eImageFormat::D32_Float:
		case eImageFormat::D32_Float_S8_UInt:
			return true;
		default:;
		}

		return false;
	}

	static constexpr bool IsStencil(eImageFormat format)
	{
		switch (format) {
		case eImageFormat::D16_UNorm_S8_UInt:
		case eImageFormat::D32_Float_S8_UInt:
			return true;
		default:;
		}

		return false;
	}


	/**
	 * @brief Get the size of the format in bytes. For example, RGBA8 would return 4.
	 */
	static constexpr uint32 GetPixelStride(eImageFormat format)
	{
		switch (format) {
		case eImageFormat::None:
			break;

			// Color formats

		case eImageFormat::BGRA8_UNorm:
		case eImageFormat::RGBA8_SRGB:
		case eImageFormat::RGBA8_UNorm:
			return 4;

		case eImageFormat::RG32_Float:
		case eImageFormat::RGBA16_Float:
			return 8;

		case eImageFormat::RGB32_Float:
			return 12;

			// Depth formats

		case eImageFormat::D16_UNorm_S8_UInt:
			return 3;

		case eImageFormat::D32_Float:
			return 4;

		case eImageFormat::D32_Float_S8_UInt:
			return 5;

		case eImageFormat::R32_SFloat:
		case eImageFormat::R32_SInt:
		case eImageFormat::R32_UInt:
			return 4;

		case eImageFormat::R8_UInt:
		case eImageFormat::R8_UNorm:
			return 1;
		}

		return 0;
	}

	static constexpr VkImageAspectFlags GetAspectMask(const eImageFormat format)
	{
		VkImageAspectFlags aspect = 0;

		// If the format is only depth, return depth aspect
		if (IsDepth(format)) {
			aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}

		// If format is only stencil, return stencil aspect. If it is both depth and stencil, return both or'd together
		if (IsStencil(format)) {
			aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		if (aspect != 0) {
			return aspect;
		}

		// Not depth or stencil, must be colour
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}

	static constexpr eImageAspectFlag GetAspectFlag(const eImageFormat format)
	{
		return static_cast<eImageAspectFlag>(GetAspectMask(format));
	}

	/**
	 * @brief Returns the usage flags for the given format (e.g. USAGE_COLOR, USAGE_DEPTH_STENCIL)
	 */
	static constexpr VkImageUsageFlags GetFormatUsageFlags(const eImageFormat format)
	{
		if (IsDepth(format) || IsStencil(format)) {
			return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}
		else {
			return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}

		return 0;
	}

	static constexpr VkFormat ToUnderlying(const eImageFormat format)
	{
		switch (format) {
		case eImageFormat::None:
			break;

			// Color formats

		case eImageFormat::RG32_Float:
			return VK_FORMAT_R32G32_SFLOAT;
		case eImageFormat::BGRA8_UNorm:
			return VK_FORMAT_B8G8R8A8_UNORM;
		case eImageFormat::RGBA8_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case eImageFormat::RGBA8_UNorm:
			return VK_FORMAT_R8G8B8A8_UNORM;

		case eImageFormat::RGBA16_Float:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case eImageFormat::RGB32_Float:
			return VK_FORMAT_R32G32B32_SFLOAT;

			// Depth Formats

		case eImageFormat::D16_UNorm_S8_UInt:
			return VK_FORMAT_D16_UNORM_S8_UINT;
		case eImageFormat::D32_Float:
			return VK_FORMAT_D32_SFLOAT;
		case eImageFormat::D32_Float_S8_UInt:
			return VK_FORMAT_D32_SFLOAT_S8_UINT;

		case eImageFormat::R32_SInt:
			return VK_FORMAT_R32_SINT;
		case eImageFormat::R32_UInt:
			return VK_FORMAT_R32_UINT;
		case eImageFormat::R32_SFloat:
			return VK_FORMAT_R32_SFLOAT;

		case eImageFormat::R8_UInt:
			return VK_FORMAT_R8_UINT;
		case eImageFormat::R8_UNorm:
			return VK_FORMAT_R8_UNORM;
		}

		return VK_FORMAT_UNDEFINED;
	}
};


struct TransitionLayoutOverrides
{
	std::optional<VkPipelineStageFlagBits> SrcStage = std::nullopt;
	std::optional<VkPipelineStageFlagBits> DstStage = std::nullopt;
	std::optional<VkAccessFlags> SrcAccessMask = std::nullopt;
	std::optional<VkAccessFlags> DstAccessMask = std::nullopt;
};

struct ImageCubemapOptions
{
	eImageAspectFlag AspectFlag = eImageAspectFlag::Color;
};


const ImageTypeProperties ImageTypeGetProperties(eImageType image_type);

class Image
{
public:
	Image();
	Image(const Image& other);

	/**
	 * @brief Transfers an `Ref` to the normal ref counted image.
	 */
	Image(Ref<Image>&& ref);

	Image& operator=(const Image& other);
	Image& operator=(Ref<Image>&& ref);

	FX_FORCE_INLINE const ImageInfo& GetInfo() const { return Info; }

	void Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format, VkImageTiling tiling,
				VkImageUsageFlags usage, eImageAspectFlag aspect, eImageCreateFlags flags = eImageCreateFlags::None);

	void Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format,
				VkImageUsageFlags usage, eImageAspectFlag aspect, eImageCreateFlags flags = eImageCreateFlags::None);

	void CreateFromData(renderer::CommandBuffer& cmd, const ImageInfo& info, eImageCreateFlags flags);


	void UploadMip(renderer::CommandBuffer& cmd, uint32 mip_index, const Vec2u& size,
				   const Slice<const uint8>& image_data);

	/**
	 * @brief Uploads multiple mip levels to an image
	 */
	void Upload(renderer::CommandBuffer& cmd, const ImageInfo& info);

	void CopyToMip(renderer::CommandBuffer& cmd, const renderer::RawGpuBuffer& buffer, VkImageLayout final_layout,
				   Vec2u size, uint32 mip_level);


	void CopyFromBuffer(renderer::CommandBuffer& cmd, const renderer::RawGpuBuffer& buffer, VkImageLayout final_layout,
						Vec2u size, uint32 base_layer, uint32 mip_level);

	void CreateLayeredImageFromCubemap(Image& cubemap, eImageFormat image_format, VkImageAspectFlags aspect_flags,
									   ImageCubemapOptions options);

	void MarkUploaded();


	void SaveToFile(const String& path, eImageSaveFormat file_format);

	VkImage Get() const { return InternalImage; }
	FX_FORCE_INLINE bool IsInited() const { return (Get() != nullptr); }

	void DecRef();


	~Image();


public:
	eImageAspectFlag Aspect = eImageAspectFlag::Color;

	VkImage InternalImage = nullptr;
	VkImageView View = nullptr;

	VkImageLayout ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VmaAllocation Allocation = nullptr;

	ImageInfo Info {};

	TextureID ID = TextureID::Null;

private:
	RefCount* mpRefCnt = nullptr;
};


} // namespace fx
