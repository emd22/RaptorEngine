#include "FxJpegLoader.hpp"
#include "Asset/FxBaseAsset.hpp"

#include <Core/Log.hpp>
#include <jpeglib.h>

#include <Core/FxMemPool.hpp>

FxJpegLoader::Status FxJpegLoader::LoadFromFile(FxBaseAsset *asset, std::string path)
{
    const char* c_path = path.c_str();

    FILE* fp = fopen(c_path, "rb");

    if (!fp) {
        Log::Error("Could not find JPEG file at '%s'", c_path);
        return FxJpegLoader::Status::Error;
    }

    struct jpeg_error_mgr error_mgr;

    mJpegInfo.err = jpeg_std_error(&error_mgr);
    jpeg_create_decompress(&mJpegInfo);

    jpeg_stdio_src(&mJpegInfo, fp);
    jpeg_read_header(&mJpegInfo, true);

    jpeg_start_decompress(&mJpegInfo);

    printf("Read jpeg, [width=%u, height=%u]\n", mJpegInfo.output_width, mJpegInfo.output_height);

    uint32 data_size = mJpegInfo.output_width * mJpegInfo.output_height * mJpegInfo.num_components;
    mImageData = static_cast<uint8*>(FxMemPool::Alloc(data_size));

    const uint32 row_stride = mJpegInfo.num_components * mJpegInfo.output_width;

    uint8* ptr_list[1];

    while (mJpegInfo.output_scanline < mJpegInfo.output_height) {
        ptr_list[0] = (mImageData + row_stride * mJpegInfo.output_scanline);
        jpeg_read_scanlines(&mJpegInfo, ptr_list, 1);
    }

    jpeg_finish_decompress(&mJpegInfo);

    fclose(fp);

    return Status::Success;
}

void FxJpegLoader::CreateGpuResource(FxBaseAsset *asset)
{
    asset->IsUploadedToGpu = true;
    asset->IsUploadedToGpu.notify_all();
}

void FxJpegLoader::Destroy(FxBaseAsset *asset)
{
    jpeg_destroy_decompress(&mJpegInfo);
    FxMemPool::Free(mImageData);
}
