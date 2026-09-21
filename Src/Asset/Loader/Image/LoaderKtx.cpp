#include "LoaderKtx.hpp"

#include <Asset/AssetBase.hpp>
#include <Core/ArrayUtil.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>

#include <algorithm>
#include <vulkan/vulkan.h>

namespace fx {

namespace loader {

static constexpr uint32 scGlRgba8 = 0x8058;
static constexpr uint32 scGlSrgb8Alpha8 = 0x8C43;

static constexpr uint32 GetVkFormat(eImageFormat format)
{
	switch (format) {
	case eImageFormat::BGRA8_UNorm:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case eImageFormat::RGBA8_UNorm:
		return VK_FORMAT_R8G8B8A8_UNORM;
	case eImageFormat::RGBA8_SRGB:
		return VK_FORMAT_R8G8B8A8_SRGB;
	case eImageFormat::R8_UNorm:
		return VK_FORMAT_R8_UNORM;
	default:;
	}

	return VK_FORMAT_UNDEFINED;
}

static constexpr eImageFormat GetImageFormatFromVk(uint32 vk_format)
{
	switch (vk_format) {
	case VK_FORMAT_B8G8R8A8_UNORM:
		return eImageFormat::BGRA8_UNorm;
	case VK_FORMAT_R8G8B8A8_UNORM:
		return eImageFormat::RGBA8_UNorm;
	case VK_FORMAT_R8G8B8A8_SRGB:
		return eImageFormat::RGBA8_SRGB;
	case VK_FORMAT_R8_UNORM:
		return eImageFormat::R8_UNorm;
	default:;
	}

	return eImageFormat::None;
}

bool LoaderKtx::AdoptTexture(ktxTexture* texture)
{
	Destroy();

	eImageFormat format = eImageFormat::None;

	if (texture->classId == ktxTexture2_c) {
		ktxTexture2* texture2 = reinterpret_cast<ktxTexture2*>(texture);

		if (ktxTexture2_NeedsTranscoding(texture2)) {
			LogError(LC_ASSET, "KTX: GPU-compressed (Basis) textures are not supported");
			ktxTexture_Destroy(texture);
			return false;
		}

		format = GetImageFormatFromVk(texture2->vkFormat);
	}
	else {
		ktxTexture1* texture1 = reinterpret_cast<ktxTexture1*>(texture);

		if (texture1->glInternalformat == scGlRgba8) {
			format = eImageFormat::RGBA8_UNorm;
		}
		else if (texture1->glInternalformat == scGlSrgb8Alpha8) {
			format = eImageFormat::RGBA8_SRGB;
		}
	}

	if (format == eImageFormat::None) {
		LogError(LC_ASSET, "KTX: unsupported pixel format");
		ktxTexture_Destroy(texture);
		return false;
	}

	if (texture->numDimensions != 2 || texture->numLayers != 1 || texture->numFaces != 1) {
		LogError(LC_ASSET, "KTX: only plain 2D textures are supported");
		ktxTexture_Destroy(texture);
		return false;
	}

	mpTexture = texture;
	mFormat = format;

	return true;
}

bool LoaderKtx::Open(const char* path)
{
	ktxTexture* texture = nullptr;

	const KTX_error_code result =
		ktxTexture_CreateFromNamedFile(path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);

	if (result != KTX_SUCCESS) {
		LogError(LC_ASSET, "Could not load KTX file at '{}': {}", path, ktxErrorString(result));
		return false;
	}

	return AdoptTexture(texture);
}

eLoaderStatus LoaderKtx::Load(AssetTicket& ticket, const std::string& path)
{
	if (!Open(path.c_str())) {
		return eLoaderStatus::Error;
	}

	static_cast<Image*>(ticket.Get())->Info.Size = GetImageSize();

	return eLoaderStatus::Success;
}

eLoaderStatus LoaderKtx::Load(AssetTicket& ticket, const uint8* data, uint32 size)
{
	Assert(data != nullptr);
	Assert(size > 0);

	ktxTexture* texture = nullptr;

	const KTX_error_code result =
		ktxTexture_CreateFromMemory(data, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);

	if (result != KTX_SUCCESS) {
		LogError(LC_ASSET, "Could not load KTX from memory: {}", ktxErrorString(result));
		return eLoaderStatus::Error;
	}

	if (!AdoptTexture(texture)) {
		return eLoaderStatus::Error;
	}

	static_cast<Image*>(ticket.Get())->Info.Size = GetImageSize();

	return eLoaderStatus::Success;
}

Vec2u LoaderKtx::GetMipDimensions(uint32 mip_level) const
{
	return Vec2u(std::max(1U, mpTexture->baseWidth >> mip_level), std::max(1U, mpTexture->baseHeight >> mip_level));
}

Slice<const uint8> LoaderKtx::GetMipData(uint32 mip_level) const
{
	Assert(mpTexture != nullptr);
	Assert(mip_level < mpTexture->numLevels);

	ktx_size_t offset = 0;
	if (ktxTexture_GetImageOffset(mpTexture, mip_level, 0, 0, &offset) != KTX_SUCCESS) {
		return Slice<const uint8>(nullptr, 0);
	}

	return Slice<const uint8>(ktxTexture_GetData(mpTexture) + offset,
							  static_cast<uint32>(ktxTexture_GetImageSize(mpTexture, mip_level)));
}

ImageInfo LoaderKtx::MakeImageInfo(uint32 first_mip, uint32 mip_count) const
{
	Assert(mpTexture != nullptr);
	Assert(first_mip < mpTexture->numLevels);

	if (mip_count == 0 || first_mip + mip_count > mpTexture->numLevels) {
		mip_count = mpTexture->numLevels - first_mip;
	}

	const uint32 end_mip = first_mip + mip_count;

	uint64 total_size = 0;
	for (uint32 level = first_mip; level < end_mip; level++) {
		total_size += GetMipData(level).Size;
	}

	uint8* buffer = static_cast<uint8*>(std::malloc(total_size));

	uint64 offset = 0;
	for (uint32 level = first_mip; level < end_mip; level++) {
		const Slice<const uint8> mip = GetMipData(level);

		memcpy(buffer + offset, mip.pData, mip.Size);
		offset += mip.Size;
	}

	ImageInfo info {};
	info.Size = GetMipDimensions(first_mip);
	info.Format = mFormat;
	info.MipLevel = 0;
	info.MipCount = mip_count;
	info.ImageData = Slice<const uint8>(buffer, total_size);

	return info;
}

void LoaderKtx::CreateGpuResource(AssetTicket& ticket)
{
	Image* image = static_cast<Image*>(ticket.Get());

	// Only the base level is uploaded, matching the other image loaders.
	ImageInfo image_info(GetImageSize(), mFormat, 0, 1, GetMipData(0));
	image_info.ImageType = ImageType;

	image->CreateFromData(renderer::GraphicsBackendFwd::GetUploadCmd(), image_info, CreationFlags);

	ticket.SignalUploadedToGpu();
}

eLoaderStatus LoaderKtx::SaveToFile(const String& path, eImageFormat format, const Vec2u& size,
									const Slice<const Slice<const uint8>>& mips)
{
	const uint32 vk_format = GetVkFormat(format);
	if (vk_format == VK_FORMAT_UNDEFINED) {
		LogError(LC_ASSET, "KTX: cannot save image with unsupported pixel format");
		return eLoaderStatus::Error;
	}

	ktxTextureCreateInfo create_info {};
	create_info.vkFormat = vk_format;
	create_info.baseWidth = size.X;
	create_info.baseHeight = size.Y;
	create_info.baseDepth = 1;
	create_info.numDimensions = 2;
	create_info.numLevels = mips.Size;
	create_info.numLayers = 1;
	create_info.numFaces = 1;
	create_info.isArray = KTX_FALSE;
	create_info.generateMipmaps = KTX_FALSE;

	ktxTexture2* texture = nullptr;

	KTX_error_code result = ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
	if (result != KTX_SUCCESS) {
		LogError(LC_ASSET, "KTX: could not create texture for '{}': {}", path, ktxErrorString(result));
		return eLoaderStatus::Error;
	}

	for (uint32 level = 0; level < mips.Size && result == KTX_SUCCESS; level++) {
		result = ktxTexture_SetImageFromMemory(ktxTexture(texture), level, 0, 0, mips.pData[level].pData,
											   mips.pData[level].Size);
	}

	if (result == KTX_SUCCESS) {
		result = ktxTexture_WriteToNamedFile(ktxTexture(texture), path.CStr());
	}

	ktxTexture_Destroy(ktxTexture(texture));

	if (result != KTX_SUCCESS) {
		LogError(LC_ASSET, "KTX: could not write '{}': {}", path, ktxErrorString(result));
		return eLoaderStatus::Error;
	}

	return eLoaderStatus::Success;
}

void LoaderKtx::Destroy()
{
	if (mpTexture != nullptr) {
		ktxTexture_Destroy(mpTexture);
		mpTexture = nullptr;
	}
}

} // namespace loader

} // namespace fx
