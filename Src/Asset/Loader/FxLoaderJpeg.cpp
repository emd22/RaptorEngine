#include "FxLoaderJpeg.hpp"

#include "Asset/FxAssetBase.hpp"

#include <jpeglib.h>

#include <Asset/FxAssetImage.hpp>
#include <Core/FxMemory.hpp>
#include <Core/FxRef.hpp>
#include <Core/Log.hpp>

FxLoaderJpeg::Status FxLoaderJpeg::LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path)
{
    FxRef<FxAssetImage> image(asset);

    const char* c_path = path.c_str();

    FILE* fp = fopen(c_path, "rb");

    if (!fp) {
        FxLogError("Could not find JPEG file at '{:s}'", c_path);
        return FxLoaderJpeg::Status::eError;
    }

    struct jpeg_error_mgr error_mgr;

    mJpegInfo.err = jpeg_std_error(&error_mgr);
    jpeg_create_decompress(&mJpegInfo);

    jpeg_stdio_src(&mJpegInfo, fp);
    jpeg_read_header(&mJpegInfo, true);

    mJpegInfo.out_color_space = JCS_EXT_RGBA;

    jpeg_start_decompress(&mJpegInfo);

    printf("Image has %d components.\n", mJpegInfo.output_components);
    image->NumComponents = mJpegInfo.output_components;

    printf("Read jpeg, [width=%u, height=%u]\n", mJpegInfo.output_width, mJpegInfo.output_height);
    image->Size = { mJpegInfo.output_width, mJpegInfo.output_height };

    uint32 data_size = mJpegInfo.output_width * mJpegInfo.output_height * image->NumComponents;
    mImageData.InitSize(data_size);

    const uint32 row_stride = image->NumComponents * mJpegInfo.output_width;

    uint8* ptr_list[1];

    while (mJpegInfo.output_scanline < mJpegInfo.output_height) {
        ptr_list[0] = (mImageData.pData + (row_stride * mJpegInfo.output_scanline));
        jpeg_read_scanlines(&mJpegInfo, ptr_list, 1);
    }

    jpeg_finish_decompress(&mJpegInfo);

    fclose(fp);

    return Status::eSuccess;
}

FxLoaderJpeg::Status FxLoaderJpeg::LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size)
{
    FxRef<FxAssetImage> image(asset);

    struct jpeg_error_mgr error_mgr;

    mJpegInfo.err = jpeg_std_error(&error_mgr);
    jpeg_create_decompress(&mJpegInfo);

    assert(data != nullptr);

    jpeg_mem_src(&mJpegInfo, data, size);

    jpeg_read_header(&mJpegInfo, true);

    mJpegInfo.out_color_space = JCS_EXT_RGBA;
    uint32 requested_components = 4;

    jpeg_start_decompress(&mJpegInfo);

    printf("Image has %d components.\n", mJpegInfo.output_components);
    image->NumComponents = mJpegInfo.output_components;
    FxDebugAssert(image->NumComponents == requested_components)

        printf("Read jpeg, [width=%u, height=%u]\n", mJpegInfo.output_width, mJpegInfo.output_height);
    image->Size = { mJpegInfo.output_width, mJpegInfo.output_height };

    uint32 data_size = mJpegInfo.output_width * mJpegInfo.output_height * image->NumComponents;
    mImageData.InitSize(data_size);

    const uint32 row_stride = image->NumComponents * mJpegInfo.output_width;

    uint8* ptr_list[1];

    while (mJpegInfo.output_scanline < mJpegInfo.output_height) {
        ptr_list[0] = (mImageData.pData + (row_stride * mJpegInfo.output_scanline));
        jpeg_read_scanlines(&mJpegInfo, ptr_list, 1);
    }

    jpeg_finish_decompress(&mJpegInfo);

    return Status::eSuccess;
}

void FxLoaderJpeg::CreateGpuResource(FxRef<FxAssetBase>& asset)
{
    FxRef<FxAssetImage> image(asset);

    image->Texture.Create(image->ImageType, mImageData, image->Size, VK_FORMAT_R8G8B8A8_SRGB, 4);

    asset->bIsUploadedToGpu = true;
    asset->bIsUploadedToGpu.notify_all();
}

void FxLoaderJpeg::Destroy(FxRef<FxAssetBase>& asset)
{
    //    while (!asset->bIsUploadedToGpu) {
    //        asset->bIsUploadedToGpu.wait(true);
    //    }

    jpeg_destroy_decompress(&mJpegInfo);
}
