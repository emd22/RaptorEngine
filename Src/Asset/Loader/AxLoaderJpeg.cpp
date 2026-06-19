#include "AxLoaderJpeg.hpp"

#include "Asset/AxBase.hpp"

#include <TurboJPEG/jpeglib.h>

#include <Asset/AxImage.hpp>
#include <Core/Memory.hpp>
#include <Core/Ref.hpp>
#include <Renderer/Backend/RenderBackendFwd.hpp>

namespace fx {


static constexpr J_COLOR_SPACE GetJpegColorspaceForFormat(eImageFormat format)
{
    switch (format) {
    case eImageFormat::BGRA8_UNorm:
        return JCS_EXT_BGRA;
    case eImageFormat::RGBA8_UNorm:
    case eImageFormat::RGBA8_SRGB:
        return JCS_EXT_RGBA;
    default:;
    }

    return JCS_EXT_RGBA;
}


AxLoaderJpeg::Status AxLoaderJpeg::LoadFromFile(TSRef<AxBase> asset, const String& path)
{
    TSRef<AxImage> image(asset);

    const char* c_path = path.CStr();

    FILE* fp = fopen(c_path, "rb");

    if (!fp) {
        LogError(LC_ASSET, "Could not find JPEG file at '{:s}'", c_path);
        return AxLoaderJpeg::Status::Error;
    }

    struct jpeg_error_mgr error_mgr;

    mJpegInfo.err = jpeg_std_error(&error_mgr);
    jpeg_create_decompress(&mJpegInfo);

    jpeg_stdio_src(&mJpegInfo, fp);
    jpeg_read_header(&mJpegInfo, true);

    mJpegInfo.out_color_space = GetJpegColorspaceForFormat(ImageFormat);

    jpeg_start_decompress(&mJpegInfo);

    int32 num_jpeg_components = mJpegInfo.output_components;

    printf("Read jpeg, [width=%u, height=%u]\n", mJpegInfo.output_width, mJpegInfo.output_height);
    image->Image.Size = { mJpegInfo.output_width, mJpegInfo.output_height };

    uint32 data_size = mJpegInfo.output_width * mJpegInfo.output_height * num_jpeg_components;
    mImageData.InitSize(data_size);

    const uint32 row_stride = num_jpeg_components * mJpegInfo.output_width;

    uint8* ptr_list[1] = { nullptr };

    while (mJpegInfo.output_scanline < mJpegInfo.output_height) {
        ptr_list[0] = (mImageData.pData + (row_stride * mJpegInfo.output_scanline));
        jpeg_read_scanlines(&mJpegInfo, ptr_list, 1);
    }

    jpeg_finish_decompress(&mJpegInfo);

    fclose(fp);

    return Status::Success;
}

AxLoaderJpeg::Status AxLoaderJpeg::LoadFromMemory(TSRef<AxBase> asset, const uint8* data, uint32 size)
{
    TSRef<AxImage> image(asset);

    struct jpeg_error_mgr error_mgr;

    mJpegInfo.err = jpeg_std_error(&error_mgr);
    jpeg_create_decompress(&mJpegInfo);

    Assert(data != nullptr);

    jpeg_mem_src(&mJpegInfo, data, size);
    jpeg_read_header(&mJpegInfo, true);

    const uint32 num_components = renderer::ImageFormatUtil::GetPixelStride(ImageFormat);

    J_COLOR_SPACE color_space = GetJpegColorspaceForFormat(ImageFormat);
    mJpegInfo.out_color_space = color_space;

    jpeg_start_decompress(&mJpegInfo);
    image->Image.Size = { mJpegInfo.output_width, mJpegInfo.output_height };

    uint32 data_size = mJpegInfo.output_width * mJpegInfo.output_height * num_components;
    mImageData.InitSize(data_size);

    const uint32 row_stride = num_components * mJpegInfo.output_width;

    uint8* ptr_list[1] = { nullptr };

    while (mJpegInfo.output_scanline < mJpegInfo.output_height) {
        ptr_list[0] = (mImageData.pData + (row_stride * mJpegInfo.output_scanline));
        jpeg_read_scanlines(&mJpegInfo, ptr_list, 1);
    }

    jpeg_finish_decompress(&mJpegInfo);

    return Status::Success;
}

void AxLoaderJpeg::CreateGpuResource(TSRef<AxBase>& asset)
{
    TSRef<AxImage> image(asset);

    const bool should_save_data = (CreationFlags & eImageCreateFlags::KeepInMemory) != 0;

    // Pass all flags that are not KeepInMemory. We will instead move the data over to avoid the copy.
    image->Image.CreateFromData(renderer::RenderBackendFwd::GetUploadCmd(), image->ImageType, image->Image.Size, 1,
                                ImageFormat, MakeSlice<const uint8>(mImageData.pData, mImageData.Size),
                                (CreationFlags & (~eImageCreateFlags::KeepInMemory)));

    if (should_save_data) {
        image->Image.ImageData = std::move(mImageData);

        // Set image data to null to avoid it being destroyed.
        mImageData.pData = nullptr;
        mImageData.Size = 0;
    }

    image->bIsUploadedToGpu = true;
    image->bIsUploadedToGpu.notify_all();
}

void AxLoaderJpeg::Destroy(TSRef<AxBase>& asset)
{
    //    while (!asset->bIsUploadedToGpu) {
    //        asset->bIsUploadedToGpu.wait(true);
    //    }

    jpeg_destroy_decompress(&mJpegInfo);
}

} // namespace fx
