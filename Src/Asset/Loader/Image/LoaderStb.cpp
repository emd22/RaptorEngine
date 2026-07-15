#include "LoaderStb.hpp"

#include <Asset/AssetBase.hpp>
#include <Asset/AxImage.hpp>
#include <Renderer/Backend/RenderBackendFwd.hpp>

namespace fx {

namespace loader {

eLoaderStatus LoaderStb::Load(AssetTicket& ticket, const std::string& path)
{
	Image* image = static_cast<Image*>(ticket.Get());

	const char* c_path = path.c_str();

	const int pixel_size = ImageFormatUtil::GetPixelStride(ImageFormat);
	Assert(pixel_size > 0);

	stbi_info(c_path, &mWidth, &mHeight, &mChannels);

	image->Info.Size = Vec2u { uint32(mWidth), uint32(mHeight) };

	uint32 data_size = mWidth * mHeight * pixel_size;
	mDataSize = data_size;

	mImageData = stbi_load(c_path, &mWidth, &mHeight, &mChannels, pixel_size);
	if (mImageData == nullptr) {
		LogError(LC_ASSET, "Could not load image file at '{}'", path);
		return eLoaderStatus::Error;
	}

	return eLoaderStatus::Success;
}

eLoaderStatus LoaderStb::Load(AssetTicket& ticket, const uint8* data, uint32 size)
{
	Image* image = static_cast<Image*>(ticket.Get());

	const int pixel_size = ImageFormatUtil::GetPixelStride(ImageFormat);
	Assert(pixel_size > 0);

	Assert(data != nullptr);
	Assert(size > 0);

	if (!stbi_info_from_memory(data, size, &mWidth, &mHeight, &mChannels)) {
		LogError(LC_ASSET, "Could not retrieve info from image in memory! (Size={})", size);
		return eLoaderStatus::Error;
	}

	mChannels = pixel_size;

	uint32 data_size = mWidth * mHeight * (pixel_size * sizeof(uint8));
	mDataSize = data_size;

	image->Info.Size = Vec2u { uint32(mWidth), uint32(mHeight) };

	mImageData = stbi_load_from_memory(data, size, &mWidth, &mHeight, &mChannels, pixel_size);

	if (mImageData == nullptr) {
		LogError(LC_ASSET, "Could not load image file from memory!");
		return eLoaderStatus::Error;
	}

	return eLoaderStatus::Success;
}


eLoaderStatus LoaderStb::SaveToFile(eImageSaveFormat file_format, const Slice<uint8>& data, const Vec2u& size,
									const String& path, eImageSaveFlags flags)
{
	if ((flags & eImageSaveFlags::FlipY) != 0) {
		stbi_flip_vertically_on_write(1);
	}

	switch (file_format) {
	case eImageSaveFormat::Jpeg:
		stbi_write_jpg(path.CStr(), size.GetX(), size.GetY(), 4, data.pData, 90);
		break;
	case eImageSaveFormat::Png:
		stbi_write_png(path.CStr(), size.GetX(), size.GetY(), 4, data.pData, size.GetX() * 4);
	}

	return eLoaderStatus::Success;
}

void LoaderStb::CreateGpuResource(AssetTicket& ticket)
{
	Image* image = static_cast<Image*>(ticket.Get());

	SizedArray<uint8> data_arr;

	// Since the image data in mImageData is not ours(it is stb's),
	// we need to set this flag to prevent it from attemping to be freed.

	data_arr.pData = mImageData;
	data_arr.Size = mDataSize;
	data_arr.Capacity = mDataSize;

	// const bool should_save_data = (CreationFlags & eImageCreateFlags::KeepInMemory) != 0;

	// Pass all flags that are not KeepInMemory. We will instead move the data over to avoid the copy.
	ImageInfo image_info { image->Info.Size, ImageFormat, 0, 1, Slice<const uint8>(data_arr.pData, data_arr.Size) };
	image->CreateFromData(renderer::RenderBackendFwd::GetUploadCmd(), image_info, (CreationFlags));

	// Set to nullptr so that the data is not freed by the SizedArray
	data_arr.pData = nullptr;

	ticket.SignalUploadedToGpu();
}

// void LoaderStb::LoadCubemapToLayeredImage(const Image&)
// {
//     Vec2u cubemap_size = cubemap_image.Size;

//     // Here is the type of cubemap we will be looking for here:
//     //
//     // T -> Top, B -> Bottom, L -> Left, R -> Right
//     // FW -> Forward, BW -> Backward
//     //
//     // +-----+-----+-----+-----+
//     // |     |  T  |     |     |
//     // +-----+-----+-----+-----+
//     // |  L  |  FW |  R  |  BW |
//     // +-----+-----+-----+-----+
//     // |     |  B  |     |     |
//     // +-----+-----+-----+-----+
//     //
//     // Note that it is 4 tiles wide and 3 tiles tall.

//     const uint32 tile_width = cubemap_size.X / 4;
//     const uint32 tile_height = cubemap_size.Y / 3;

//     if (tile_width != tile_height) {
//         LogWarning("Cubemap tile width != cubemap tile height!");
//     }

//     StackArray<VkImageCopy, 6> image_copy_infos;
// }

void LoaderStb::Destroy()
{
	// while (!asset->bIsUploadedToGpu) {
	//     asset->bIsUploadedToGpu.wait(true);
	// }

	if (mImageData != nullptr) {
		stbi_image_free(mImageData);
	}
}

} // namespace loader

} // namespace fx
