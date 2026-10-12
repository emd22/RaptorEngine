#include "LoaderKtx.hpp"

#include <vulkan/vulkan.h>

#include <Asset/AssetBase.hpp>
#include <Core/ArrayUtil.hpp>
#include <Renderer/Backend/Device.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <algorithm>
#include <bit>

namespace fx {

namespace loader {

static constexpr uint32 scGlRgba8 = 0x8058;
static constexpr uint32 scGlSrgb8Alpha8 = 0x8C43;

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
	case VK_FORMAT_BC7_UNORM_BLOCK:
		return eImageFormat::BC7_UNorm;
	case VK_FORMAT_BC7_SRGB_BLOCK:
		return eImageFormat::BC7_SRGB;
	default:;
	}

	return eImageFormat::None;
}

static uint32 ReadBlockBits(const uint64 block[2], uint32 offset, uint32 count)
{
	const uint32 word = offset >> 6;
	const uint32 shift = offset & 63;

	uint64 bits = block[word] >> shift;

	if (shift + count > 64) {
		bits |= block[word + 1] << (64 - shift);
	}

	return static_cast<uint32>(bits & ((1ULL << count) - 1));
}

static bool IsBc7BlockOpaque(const uint8* data)
{
	uint64 block[2];
	memcpy(block, data, sizeof(block));

	const auto are_endpoints_opaque = [&block](uint32 offset, uint32 bits)
	{
		const uint32 max_value = (1U << bits) - 1;
		return ReadBlockBits(block, offset, bits) == max_value && ReadBlockBits(block, offset + bits, bits) == max_value;
	};

	switch (std::countr_zero(static_cast<uint32>(block[0] & 0xFF) | 0x100U)) {
	case 0:
	case 1:
	case 2:
	case 3:
		return true;
	case 4:
	{
		static constexpr uint32 offsets[4] = { 38, 8, 18, 28 };
		const uint32 rotation = ReadBlockBits(block, 5, 2);
		return are_endpoints_opaque(offsets[rotation], (rotation == 0) ? 6 : 5);
	}
	case 5:
	{
		static constexpr uint32 offsets[4] = { 50, 8, 22, 36 };
		const uint32 rotation = ReadBlockBits(block, 6, 2);
		return are_endpoints_opaque(offsets[rotation], (rotation == 0) ? 8 : 7);
	}
	case 6:
		return are_endpoints_opaque(49, 7);
	case 7:
		return ReadBlockBits(block, 74, 20) == 0xFFFFF;
	default:;
	}

	return false;
}

bool LoaderKtx::AdoptTexture(ktxTexture* texture)
{
	Destroy();

	eImageFormat format = eImageFormat::None;

	if (texture->classId == ktxTexture2_c) {
		ktxTexture2* texture2 = reinterpret_cast<ktxTexture2*>(texture);

		const bool supports_bc7 = renderer::GraphicsBackendFwd::GetDevice()->bSupportsBC7;

		if (ktxTexture2_NeedsTranscoding(texture2)) {
			const ktx_transcode_fmt_e target = supports_bc7 ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32;
			const KTX_error_code result = ktxTexture2_TranscodeBasis(texture2, target, 0);

			if (result != KTX_SUCCESS) {
				LogError(LC_ASSET, "KTX: could not transcode to {}: {}", ktxTranscodeFormatString(target),
						 ktxErrorString(result));
				ktxTexture_Destroy(texture);
				return false;
			}
		}

		format = GetImageFormatFromVk(texture2->vkFormat);

		if (ImageFormatUtil::IsBlockCompressed(format) && !supports_bc7) {
			LogError(LC_ASSET, "KTX: the texture is BC7 compressed, which this device does not support");
			ktxTexture_Destroy(texture);
			return false;
		}
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

	const KTX_error_code result = ktxTexture_CreateFromNamedFile(path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
																 &texture);

	if (result != KTX_SUCCESS) {
		LogError(LC_ASSET, "Could not load KTX file at '{}': {}", path, ktxErrorString(result));
		return false;
	}

	return AdoptTexture(texture);
}

eLoaderStatus LoaderKtx::Load(AssetTicket& ticket, const String& path)
{
	if (!Open(path.CStr())) {
		return eLoaderStatus::Error;
	}

	static_cast<Image*>(ticket.Get())->Info.Size = GetImageSize();

	return eLoaderStatus::Success;
}

bool LoaderKtx::OpenFromMemory(const uint8* data, uint32 size)
{
	Assert(data != nullptr);
	Assert(size > 0);

	ktxTexture* texture = nullptr;

	// LOAD_IMAGE_DATA copies the levels into the texture's own storage, so `data` is not referenced afterwards
	const KTX_error_code result = ktxTexture_CreateFromMemory(data, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
															  &texture);

	if (result != KTX_SUCCESS) {
		LogError(LC_ASSET, "Could not load KTX from memory: {}", ktxErrorString(result));
		return false;
	}

	return AdoptTexture(texture);
}

eLoaderStatus LoaderKtx::Load(AssetTicket& ticket, const uint8* data, uint32 size)
{
	if (!OpenFromMemory(data, size)) {
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

	if (total_size == 0) {
		LogError(LC_ASSET, "KTX: no pixel data in levels [{}, {})", first_mip, end_mip);
		return ImageInfo {};
	}

	uint8* buffer = static_cast<uint8*>(std::malloc(total_size));

	if (buffer == nullptr) {
		LogError(LC_ASSET, "KTX: could not allocate {} bytes for {} mip levels", total_size, mip_count);
		return ImageInfo {};
	}

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
	// Caller's to release, via ImageInfo::FreeOwnedData()
	info.bOwnsData = true;

	return info;
}

bool LoaderKtx::HasTranslucentTexels() const
{
	Assert(mpTexture != nullptr);

	const Slice<const uint8> base = GetMipData(0);

	if (base.pData == nullptr) {
		return false;
	}

	if (ImageFormatUtil::IsBlockCompressed(mFormat)) {
		for (uint64 offset = 0; offset + 16 <= base.Size; offset += 16) {
			if (!IsBc7BlockOpaque(base.pData + offset)) {
				return true;
			}
		}

		return false;
	}

	if (ImageFormatUtil::GetPixelStride(mFormat) != 4) {
		return false;
	}

	for (uint64 offset = 3; offset < base.Size; offset += 4) {
		if (base.pData[offset] != 0xFF) {
			return true;
		}
	}

	return false;
}

void LoaderKtx::CreateGpuResource(AssetTicket& ticket)
{
	Image* image = static_cast<Image*>(ticket.Get());

	ImageInfo chain_info {};

	if (mpTexture->numLevels > 1 &&
		(ImageFormatUtil::GetPixelStride(mFormat) == 4 || ImageFormatUtil::IsBlockCompressed(mFormat))) {
		chain_info = MakeImageInfo();
	}

	if (chain_info.ImageData.pData != nullptr) {
		chain_info.ImageType = ImageType;

		image->Upload(renderer::GraphicsBackendFwd::GetUploadCmd(), chain_info);
		chain_info.FreeOwnedData();
	}
	else {
		ImageInfo image_info(GetImageSize(), mFormat, 0, 1, GetMipData(0));
		image_info.ImageType = ImageType;

		image->CreateFromData(renderer::GraphicsBackendFwd::GetUploadCmd(), image_info, CreationFlags);
	}

	ticket.SignalUploadedToGpu();
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
