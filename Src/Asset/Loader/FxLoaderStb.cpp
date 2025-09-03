#include "FxLoaderStb.hpp"

#include <Asset/FxAssetBase.hpp>
#include <Asset/FxAssetImage.hpp>

FxLoaderStb::Status FxLoaderStb::LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path)
{
    FxRef<FxAssetImage> image(asset);

    const char* c_path = path.c_str();

    const int requested_channels = 4; /* RGBA */

    stbi_info(c_path, &mWidth, &mHeight, &mChannels);

    image->NumComponents = mChannels;
    image->Size = { uint32(mWidth), uint32(mHeight) };

    uint32 data_size = mWidth * mHeight * requested_channels;
    mDataSize = data_size;

    mImageData = stbi_load(c_path, &mWidth, &mHeight, &mChannels, requested_channels);
    if (mImageData == nullptr) {
        FxLogError("Could not load image file at '{:d}'", c_path);
        return FxLoaderStb::Status::Error;
    }

    return FxLoaderStb::Status::Success;
}

FxLoaderStb::Status FxLoaderStb::LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size)
{
    FxRef<FxAssetImage> image(asset);

    const int requested_channels = 4; /* RGBA */

    if (!stbi_info_from_memory(data, size, &mWidth, &mHeight, &mChannels)) {
        FxLogError("Could not retrieve info from image in memory! (Size:{u})", size);
        return FxLoaderStb::Status::Error;
    }

    mChannels = requested_channels;

    uint32 data_size = mWidth * mHeight * (requested_channels * sizeof(uint8));
    mDataSize = data_size;

    image->NumComponents = mChannels;
    image->Size = { uint32(mWidth), uint32(mHeight) };

    mImageData = stbi_load_from_memory(data, size, &mWidth, &mHeight, &mChannels, requested_channels);

    if (mImageData == nullptr) {
        FxLogError("Could not load image file from memory!");
        return FxLoaderStb::Status::Error;
    }

    return FxLoaderStb::Status::Success;
}

void FxLoaderStb::CreateGpuResource(FxRef<FxAssetBase>& asset)
{
    FxRef<FxAssetImage> image(asset);

    FxSizedArray<uint8> data_arr;

    // Since the image data in mImageData is not ours(it is stb's),
    // we need to set this flag to prevent it from attemping to be freed.
    data_arr.DoNotDestroy = true;

    data_arr.Data = mImageData;
    data_arr.Size = mDataSize;
    data_arr.Capacity = mDataSize;

    image->Texture.Create(image->ImageType, data_arr, image->Size, VK_FORMAT_R8G8B8A8_SRGB, 4);

    asset = image;

    asset->IsUploadedToGpu = true;
    asset->IsUploadedToGpu.notify_all();
}

void FxLoaderStb::Destroy(FxRef<FxAssetBase>& asset)
{
    // while (!asset->IsUploadedToGpu) {
    //     asset->IsUploadedToGpu.wait(true);
    // }

    stbi_image_free(mImageData);
}
