#include "LoaderKtx.hpp"

#include <Asset/AssetBase.hpp>
#include <Core/ArrayUtil.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Util/RustInterop.hpp>

#include <algorithm>

namespace fx {

namespace loader {

static const RxLogSink scKtxLog = { .user = nullptr, .log = RustInterop::Log };

bool LoaderKtx::Adopt(RxKtx* ktx)
{
	Destroy();

	if (ktx == nullptr) {
		return false;
	}

	RxKtxInfo info {};
	rx_ktx_info(ktx, &info);

	mpKtx = ktx;
	mFormat = static_cast<eImageFormat>(info.format);
	mSize = Vec2u(info.width, info.height);
	mMipCount = info.level_count;

	return true;
}

bool LoaderKtx::Open(const char* path) { return Adopt(rx_ktx_open_file(path, &scKtxLog)); }

eLoaderStatus LoaderKtx::Load(AssetTicket& ticket, const std::string& path)
{
	if (!Open(path.c_str())) {
		return eLoaderStatus::Error;
	}

	static_cast<Image*>(ticket.Get())->SetSize(GetImageSize());

	return eLoaderStatus::Success;
}

bool LoaderKtx::OpenFromMemory(const uint8* data, uint32 size)
{
	Assert(data != nullptr);
	Assert(size > 0);

	return Adopt(rx_ktx_open_memory(data, size, &scKtxLog));
}

eLoaderStatus LoaderKtx::Load(AssetTicket& ticket, const uint8* data, uint32 size)
{
	if (!OpenFromMemory(data, size)) {
		return eLoaderStatus::Error;
	}

	static_cast<Image*>(ticket.Get())->SetSize(GetImageSize());

	return eLoaderStatus::Success;
}

Vec2u LoaderKtx::GetMipDimensions(uint32 mip_level) const
{
	return Vec2u(std::max(1U, mSize.X >> mip_level), std::max(1U, mSize.Y >> mip_level));
}

Slice<const uint8> LoaderKtx::GetMipData(uint32 mip_level) const
{
	Assert(mpKtx != nullptr);
	Assert(mip_level < mMipCount);

	size_t size = 0;
	const uint8* data = rx_ktx_level(mpKtx, mip_level, &size);

	return Slice<const uint8>(data, static_cast<uint32>(size));
}

ImageInfo LoaderKtx::MakeImageInfo(uint32 first_mip, uint32 mip_count) const
{
	Assert(mpKtx != nullptr);
	Assert(first_mip < mMipCount);

	if (mip_count == 0 || first_mip + mip_count > mMipCount) {
		mip_count = mMipCount - first_mip;
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

void LoaderKtx::CreateGpuResource(AssetTicket& ticket)
{
	Image* image = static_cast<Image*>(ticket.Get());

	ImageInfo chain_info {};

	if (mMipCount > 1 && ImageFormatUtil::GetPixelStride(mFormat) == 4) {
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
	if (mpKtx != nullptr) {
		rx_ktx_free(mpKtx);
		mpKtx = nullptr;
	}
}

} // namespace loader

} // namespace fx
