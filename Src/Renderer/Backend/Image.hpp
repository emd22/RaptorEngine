#pragma once

#include <vulkan/vulkan.h>

// #define VMA_DEBUG_LOG(...) OldLog::Warning(__VA_ARGS__)

#include "Commands.hpp"
#include "GpuBuffer.hpp"


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
	BGRA8_SRGB,
	RGBA8_SRGB,
	RGBA8_UNorm,

	RG32_Float,
	RG16_UNorm,

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
	CubemapArray,
};

struct ImageInfo
{
	ImageInfo() = default;

	ImageInfo(Vec2u size, eImageFormat format, int32 mip_level, int32 mip_count, const Slice<const uint8>& data)
		: Size(size), ImageType(eImageType::Flat), Format(format), MipLevel(mip_level), MipCount(mip_count),
		  ImageData(data)
	{
	}

	/// Releases `ImageData` if this info owns it, and clears the ownership either way. Safe to call more than once,
	/// and a no-op for the common case where `ImageData` points into a loader's own memory.
	void FreeOwnedData()
	{
		if (bOwnsData && ImageData.pData != nullptr) {
			std::free(const_cast<uint8*>(ImageData.pData));
		}

		ImageData = Slice<const uint8>(nullptr, 0);
		bOwnsData = false;
	}

public:
	Vec2u Size = Vec2u::sZero;

	eImageType ImageType = eImageType::Flat;
	eImageFormat Format = eImageFormat::RGBA8_UNorm;
	uint32 MipLevel = 0;
	uint32 MipCount = 1;
	Slice<const uint8> ImageData { nullptr, 0 };

	/// Whether `ImageData` is a malloc'd buffer this info is responsible for freeing. Copies of an ImageInfo are
	/// deliberately shallow, so exactly one of them carries this: whoever takes the data on clears it on the source.
	bool bOwnsData = false;
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

static_assert(static_cast<uint16>(eImageFormat::None) == RX_IMAGE_FORMAT_NONE);
static_assert(static_cast<uint16>(eImageFormat::BGRA8_UNorm) == RX_IMAGE_FORMAT_BGRA8_UNORM);
static_assert(static_cast<uint16>(eImageFormat::BGRA8_SRGB) == RX_IMAGE_FORMAT_BGRA8_SRGB);
static_assert(static_cast<uint16>(eImageFormat::RGBA8_SRGB) == RX_IMAGE_FORMAT_RGBA8_SRGB);
static_assert(static_cast<uint16>(eImageFormat::RGBA8_UNorm) == RX_IMAGE_FORMAT_RGBA8_UNORM);
static_assert(static_cast<uint16>(eImageFormat::RG32_Float) == RX_IMAGE_FORMAT_RG32_FLOAT);
static_assert(static_cast<uint16>(eImageFormat::RG16_UNorm) == RX_IMAGE_FORMAT_RG16_UNORM);
static_assert(static_cast<uint16>(eImageFormat::RGBA16_Float) == RX_IMAGE_FORMAT_RGBA16_FLOAT);
static_assert(static_cast<uint16>(eImageFormat::RGB32_Float) == RX_IMAGE_FORMAT_RGB32_FLOAT);
static_assert(static_cast<uint16>(eImageFormat::D16_UNorm_S8_UInt) == RX_IMAGE_FORMAT_D16_UNORM_S8_UINT);
static_assert(static_cast<uint16>(eImageFormat::D32_Float) == RX_IMAGE_FORMAT_D32_FLOAT);
static_assert(static_cast<uint16>(eImageFormat::D32_Float_S8_UInt) == RX_IMAGE_FORMAT_D32_FLOAT_S8_UINT);
static_assert(static_cast<uint16>(eImageFormat::R32_SFloat) == RX_IMAGE_FORMAT_R32_SFLOAT);
static_assert(static_cast<uint16>(eImageFormat::R32_SInt) == RX_IMAGE_FORMAT_R32_SINT);
static_assert(static_cast<uint16>(eImageFormat::R32_UInt) == RX_IMAGE_FORMAT_R32_UINT);
static_assert(static_cast<uint16>(eImageFormat::R8_UInt) == RX_IMAGE_FORMAT_R8_UINT);
static_assert(static_cast<uint16>(eImageFormat::R8_UNorm) == RX_IMAGE_FORMAT_R8_UNORM);

static_assert(static_cast<uint32>(eImageType::Flat) == RX_IMAGE_TYPE_FLAT);
static_assert(static_cast<uint32>(eImageType::Cubemap) == RX_IMAGE_TYPE_CUBEMAP);
static_assert(static_cast<uint32>(eImageType::CubemapArray) == RX_IMAGE_TYPE_CUBEMAP_ARRAY);

struct ImageFormatUtil
{
	static bool IsDepth(eImageFormat format) { return rx_image_format_is_depth(static_cast<uint16>(format)) != 0; }

	static bool IsStencil(eImageFormat format)
	{
		return rx_image_format_is_stencil(static_cast<uint16>(format)) != 0;
	}

	/// Whether sampling this format applies the sRGB transfer function, i.e. its contents are gamma encoded.
	static bool IsSrgb(eImageFormat format) { return rx_image_format_is_srgb(static_cast<uint16>(format)) != 0; }

	/**
	 * @brief Get the size of the format in bytes. For example, RGBA8 would return 4.
	 */
	static uint32 GetPixelStride(eImageFormat format)
	{
		return rx_image_format_pixel_stride(static_cast<uint16>(format));
	}

	static VkImageAspectFlags GetAspectMask(const eImageFormat format)
	{
		return static_cast<VkImageAspectFlags>(rx_image_format_aspect_mask(static_cast<uint16>(format)));
	}

	static eImageAspectFlag GetAspectFlag(const eImageFormat format)
	{
		return static_cast<eImageAspectFlag>(GetAspectMask(format));
	}

	/**
	 * @brief Returns the usage flags for the given format (e.g. USAGE_COLOR, USAGE_DEPTH_STENCIL)
	 */
	static VkImageUsageFlags GetFormatUsageFlags(const eImageFormat format)
	{
		return static_cast<VkImageUsageFlags>(rx_image_format_usage(static_cast<uint16>(format)));
	}

	static VkFormat ToUnderlying(const eImageFormat format)
	{
		return static_cast<VkFormat>(rx_image_format_to_vk(static_cast<uint16>(format)));
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


const ImageTypeProperties ImageTypeGetProperties(eImageType image_type, uint32 cube_count = 1);

class Image
{
public:
	Image();

	/// An image that shares the state of `record`, whether or not it has been created yet.
	explicit Image(const RxImage* record);

	Image(const Image& other);
	Image(Image&& other) noexcept;

	Image& operator=(const Image& other);
	Image& operator=(Image&& other) noexcept;

	/// The image's state lives in a record owned by Rust. Copies of a created `Image` share that record, so they all
	/// see the same handles, layout and size, and the resources are freed when the last copy goes away. Copies of an
	/// image that has not been created are independent descriptions.
	ImageInfo GetInfo() const;

	void SetInfo(Vec2u size, eImageFormat format, uint32 mip_level, uint32 mip_count);

	FX_FORCE_INLINE Vec2u GetSize() const { return Vec2u { mpRecord->width, mpRecord->height }; }
	FX_FORCE_INLINE void SetSize(Vec2u size)
	{
		mpRecord->width = size.X;
		mpRecord->height = size.Y;
	}

	FX_FORCE_INLINE eImageFormat GetFormat() const { return static_cast<eImageFormat>(mpRecord->format); }
	FX_FORCE_INLINE uint32 GetMipLevel() const { return mpRecord->mip_level; }
	FX_FORCE_INLINE void SetMipLevel(uint32 mip_level) { mpRecord->mip_level = mip_level; }
	FX_FORCE_INLINE uint32 GetMipCount() const { return mpRecord->mip_count; }
	FX_FORCE_INLINE eImageAspectFlag GetAspect() const { return static_cast<eImageAspectFlag>(mpRecord->aspect); }

	FX_FORCE_INLINE VkImageLayout GetLayout() const { return static_cast<VkImageLayout>(mpRecord->layout); }
	FX_FORCE_INLINE void SetLayout(VkImageLayout layout) { mpRecord->layout = static_cast<int32>(layout); }

	FX_FORCE_INLINE VkImage Get() const { return reinterpret_cast<VkImage>(mpRecord->image); }
	FX_FORCE_INLINE VkImageView GetView() const { return reinterpret_cast<VkImageView>(mpRecord->view); }

	/// The image handle as an integer, for hashing and passing to Rust.
	FX_FORCE_INLINE uint64 GetRawImage() const { return mpRecord->image; }
	FX_FORCE_INLINE uint64 GetRawView() const { return mpRecord->view; }

	FX_FORCE_INLINE bool IsInited() const { return mpRecord->image != 0; }

	/// The record that holds the image's state, shared by copies of the image.
	FX_FORCE_INLINE const RxImage* GetRecord() const { return mpRecord; }

	void Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format, VkImageTiling tiling,
				VkImageUsageFlags usage, eImageAspectFlag aspect, eImageCreateFlags flags = eImageCreateFlags::None,
				uint32 cube_count = 1);

	void Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format,
				VkImageUsageFlags usage, eImageAspectFlag aspect, eImageCreateFlags flags = eImageCreateFlags::None,
				uint32 cube_count = 1);

	/// Makes this image a view onto an image that something else owns, such as a swapchain image. Only the view is
	/// destroyed with it.
	void WrapExternal(VkImage image, Vec2u size, eImageFormat format);

	/// Replaces the view with a color view of the whole image.
	void RecreateColorView();

	void CreateFromData(renderer::CommandBuffer& cmd, const ImageInfo& info, eImageCreateFlags flags);


	void UploadMip(renderer::CommandBuffer& cmd, uint32 mip_index, const Vec2u& size,
				   const Slice<const uint8>& image_data);

	/**
	 * @brief Uploads multiple mip levels to an image
	 */
	void Upload(renderer::CommandBuffer& cmd, const ImageInfo& info);

	void CopyToMip(renderer::CommandBuffer& cmd, const renderer::RawGpuBuffer& buffer, VkImageLayout final_layout,
				   Vec2u size, uint32 mip_level);


	/// `dst_offset` places the copied region inside the image, for updating part of an atlas.
	void CopyFromBuffer(renderer::CommandBuffer& cmd, const renderer::RawGpuBuffer& buffer, VkImageLayout final_layout,
						Vec2u size, uint32 base_layer, uint32 mip_level, Vec2u dst_offset = Vec2u::sZero);

	void CreateLayeredImageFromCubemap(Image& cubemap, eImageFormat image_format, VkImageAspectFlags aspect_flags,
									   ImageCubemapOptions options);

	void MarkUploaded();


	void SaveToFile(const String& path, eImageSaveFormat file_format);

	~Image();

public:
	TextureID ID = TextureID::Null;

private:
	void Release();
	void ShareOrCopy(const Image& other);

private:
	RxImage* mpRecord = nullptr;
};


} // namespace fx
