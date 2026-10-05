#include "LoaderStb.hpp"

#include <Asset/AssetBase.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Util/RustInterop.hpp>

namespace fx {

namespace loader {

static const RxLogSink scImageLog = { .user = nullptr, .log = RustInterop::Log };

bool LoaderStb::Adopt(RxDecodedImage* image)
{
	Destroy();

	if (image == nullptr) {
		return false;
	}

	RxDecodedInfo info {};
	rx_image_decoded_info(image, &info);

	mpImage = image;
	mWidth = info.width;
	mHeight = info.height;
	mDataSize = static_cast<uint32>(info.size);

	return true;
}

eLoaderStatus LoaderStb::Load(AssetTicket& ticket, const std::string& path)
{
	Image* image = static_cast<Image*>(ticket.Get());

	const int pixel_size = ImageFormatUtil::GetPixelStride(ImageFormat);
	Assert(pixel_size > 0);

	if (!Adopt(rx_image_decode_file(path.c_str(), static_cast<uint32>(pixel_size)))) {
		LogError(LC_ASSET, "Could not load image file at '{}'", path);
		return eLoaderStatus::Error;
	}

	image->SetSize(Vec2u { mWidth, mHeight });

	return eLoaderStatus::Success;
}

eLoaderStatus LoaderStb::Load(AssetTicket& ticket, const uint8* data, uint32 size)
{
	Image* image = static_cast<Image*>(ticket.Get());

	const int pixel_size = ImageFormatUtil::GetPixelStride(ImageFormat);
	Assert(pixel_size > 0);

	Assert(data != nullptr);
	Assert(size > 0);

	uint32 width = 0;
	uint32 height = 0;

	if (!rx_image_probe_memory(data, size, &width, &height)) {
		LogError(LC_ASSET, "Could not retrieve info from image in memory! (Size={})", size);
		return eLoaderStatus::Error;
	}

	if (!Adopt(rx_image_decode_memory(data, size, static_cast<uint32>(pixel_size)))) {
		LogError(LC_ASSET, "Could not load image file from memory!");
		return eLoaderStatus::Error;
	}

	image->SetSize(Vec2u { mWidth, mHeight });

	return eLoaderStatus::Success;
}


eLoaderStatus LoaderStb::SaveToFile(eImageSaveFormat file_format, const Slice<const uint8>& data, const Vec2u& size,
									const String& path, eImageSaveFlags flags)
{
	const uint32 format = (file_format == eImageSaveFormat::Jpeg) ? RX_IMAGE_SAVE_JPEG : RX_IMAGE_SAVE_PNG;
	const bool flip_y = (flags & eImageSaveFlags::FlipY) != 0;

	if (!rx_image_save(path.CStr(), format, data.pData, data.Size, size.GetX(), size.GetY(), flip_y ? 1 : 0,
					   &scImageLog)) {
		return eLoaderStatus::Error;
	}

	return eLoaderStatus::Success;
}

Slice<uint8> LoaderStb::GetImageData() const
{
	if (mpImage == nullptr) {
		return Slice<uint8>(nullptr, 0);
	}

	return Slice<uint8>(const_cast<uint8*>(rx_image_decoded_data(mpImage)), mDataSize);
}

void LoaderStb::CreateGpuResource(AssetTicket& ticket)
{
	Image* image = static_cast<Image*>(ticket.Get());

	// The pixels belong to this loader and stay until Destroy(), which runs once the upload is done
	ImageInfo image_info { image->GetSize(), ImageFormat, 0, 1, Slice<const uint8>(GetImageData().pData, mDataSize) };
	image->CreateFromData(renderer::GraphicsBackendFwd::GetUploadCmd(), image_info, (CreationFlags));

	ticket.SignalUploadedToGpu();
}

void LoaderStb::Destroy()
{
	if (mpImage != nullptr) {
		rx_image_decoded_free(mpImage);
		mpImage = nullptr;
	}
}

} // namespace loader

} // namespace fx
