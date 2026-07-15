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

	/// DO NOT USE FORMAT: Marker for depth formats
	_eDepthFormatsBegin,

	eD16_UNorm_S8_UInt,
	eD32_Float,
	eD32_Float_S8_UInt,

	/// DO NOT USE FORMAT: Marker for depth formats
	_eDepthFormatsEnd,
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


struct ImageFormatUtil
{
	static constexpr bool IsDepth(eImageFormat format)
	{
		if (format > eImageFormat::_eDepthFormatsBegin && format < eImageFormat::_eDepthFormatsEnd) {
			return true;
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
		case eImageFormat::_eDepthFormatsBegin:
		case eImageFormat::_eDepthFormatsEnd:
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

		case eImageFormat::eD16_UNorm_S8_UInt:
			return 3;

		case eImageFormat::eD32_Float:
			return 4;

		case eImageFormat::eD32_Float_S8_UInt:
			return 5;
		}

		return 0;
	}


	static constexpr VkFormat ToUnderlying(eImageFormat format)
	{
		switch (format) {
		case eImageFormat::None:
		case eImageFormat::_eDepthFormatsBegin:
		case eImageFormat::_eDepthFormatsEnd:
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

		case eImageFormat::eD16_UNorm_S8_UInt:
			return VK_FORMAT_D16_UNORM_S8_UINT;
		case eImageFormat::eD32_Float:
			return VK_FORMAT_D32_SFLOAT;
		case eImageFormat::eD32_Float_S8_UInt:
			return VK_FORMAT_D32_SFLOAT_S8_UINT;
		}

		return VK_FORMAT_UNDEFINED;
	}
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

struct TransitionLayoutOverrides
{
	std::optional<VkPipelineStageFlagBits> DstStage = std::nullopt;
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
				VkImageUsageFlags usage, eImageAspectFlag aspect);

	void Create(eImageType image_type, const Vec2u& size, uint16 mips_count, eImageFormat format,
				VkImageUsageFlags usage, eImageAspectFlag aspect);

	void CreateFromData(renderer::CommandBuffer& cmd, const ImageInfo& info, eImageCreateFlags flags);


	void UploadMip(renderer::CommandBuffer& cmd, uint32 mip_index, const Vec2u& size,
				   const Slice<const uint8>& image_data);

	/**
	 * @brief Uploads multiple mip levels to an image
	 */
	void Upload(renderer::CommandBuffer& cmd, const ImageInfo& info);

	void TransitionLayout(VkImageLayout new_layout, renderer::CommandBuffer& cmd, uint32 layer_count,
						  std::optional<TransitionLayoutOverrides> overrides = std::nullopt);

	void TransitionMip(VkImageLayout new_layout, renderer::CommandBuffer& cmd, uint32 mip_level, uint32 num_levels,
					   std::optional<TransitionLayoutOverrides> overrides);


	void TransitionDepthToShaderRO(renderer::CommandBuffer& cmd);
	void TransitionDepthToAttachment(renderer::CommandBuffer& cmd);

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
