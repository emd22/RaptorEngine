#include "AxLoaderJpeg.hpp"

#include "Asset/AxBase.hpp"

#include <TurboJPEG/jpeglib.h>

#include <Asset/AxImage.hpp>
#include <Core/FxMemory.hpp>
#include <Core/FxRef.hpp>
#include <Core/Log.hpp>

static constexpr J_COLOR_SPACE GetJpegColorspaceForFormat(RxImageFormat format)
{
    switch (format) {
    case RxImageFormat::eBGRA8_UNorm:
        return JCS_EXT_BGRA;
    case RxImageFormat::eRGBA8_UNorm:
    case RxImageFormat::eRGBA8_SRGB:
        return JCS_EXT_RGBA;
    default:;
    }

    return JCS_EXT_RGBA;
}


AxLoaderJpeg::Status AxLoaderJpeg::LoadFromFile(FxRef<AxBase> asset, const std::string& path)
{
    FxRef<AxImage> image(asset);

    const char* c_path = path.c_str();

    FILE* fp = fopen(c_path, "rb");

    if (!fp) {
        FxLogError("Could not find JPEG file at '{:s}'", c_path);
        return AxLoaderJpeg::Status::eError;
    }

    struct jpeg_error_mgr error_mgr;

    mJpegInfo.err = jpeg_std_error(&error_mgr);
    jpeg_create_decompress(&mJpegInfo);

    jpeg_stdio_src(&mJpegInfo, fp);
    jpeg_read_header(&mJpegInfo, true);

    mJpegInfo.out_color_space = GetJpegColorspaceForFormat(ImageFormat);

    jpeg_start_decompress(&mJpegInfo);

    printf("Image has %d components.\n", mJpegInfo.output_components);
    int32 num_jpeg_components = mJpegInfo.output_components;

    printf("Read jpeg, [width=%u, height=%u]\n", mJpegInfo.output_width, mJpegInfo.output_height);
    image->Size = { mJpegInfo.output_width, mJpegInfo.output_height };

    uint32 data_size = mJpegInfo.output_width * mJpegInfo.output_height * num_jpeg_components;
    mImageData.InitSize(data_size);

    const uint32 row_stride = num_jpeg_components * mJpegInfo.output_width;

    uint8* ptr_list[1];

    while (mJpegInfo.output_scanline < mJpegInfo.output_height) {
        ptr_list[0] = (mImageData.pData + (row_stride * mJpegInfo.output_scanline));
        jpeg_read_scanlines(&mJpegInfo, ptr_list, 1);
    }

    jpeg_finish_decompress(&mJpegInfo);

    fclose(fp);

    return Status::eSuccess;
}

AxLoaderJpeg::Status AxLoaderJpeg::LoadFromMemory(FxRef<AxBase> asset, const uint8* data, uint32 size)
{
    FxRef<AxImage> image(asset);

    struct jpeg_error_mgr error_mgr;

    mJpegInfo.err = jpeg_std_error(&error_mgr);
    jpeg_create_decompress(&mJpegInfo);

    FxAssert(data != nullptr);

    jpeg_mem_src(&mJpegInfo, data, size);

    jpeg_read_header(&mJpegInfo, true);

    J_COLOR_SPACE color_space = JCS_EXT_RGBA;
    if (ImageNumComponents == 4) {
        color_space = JCS_EXT_RGBA;
    }
    else if (ImageNumComponents == 3) {
        color_space = JCS_EXT_RGB;
    }

    mJpegInfo.out_color_space = color_space;

    jpeg_start_decompress(&mJpegInfo);

    printf("Image has %d components\n", mJpegInfo.output_components);
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

    return Status::eSuccess;
}

void AxLoaderJpeg::CreateGpuResource(FxRef<AxBase>& asset)
{
    FxRef<AxImage> image(asset);

    image->Texture.Create(image->ImageType, mImageData, image->Size, ImageFormat, ImageNumComponents);

    asset->bIsUploadedToGpu = true;
    asset->bIsUploadedToGpu.notify_all();
}

void AxLoaderJpeg::Destroy(FxRef<AxBase>& asset)
{
    //    while (!asset->bIsUploadedToGpu) {
    //        asset->bIsUploadedToGpu.wait(true);
    //    }

    jpeg_destroy_decompress(&mJpegInfo);
}
