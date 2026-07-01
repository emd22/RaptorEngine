#pragma once

#include "../ImageLoaderBase.hpp"

#include <TurboJPEG/jpeglib.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>

namespace fx {

namespace loader {

class LoaderJpeg : public ImageLoaderBase
{
public:
    LoaderJpeg() = default;

    eLoaderStatus Load(TSRef<AssetBase> asset, const String& path) override;
    eLoaderStatus Load(TSRef<AssetBase> asset, const uint8* data, uint32 size) override;

    void Destroy(TSRef<AssetBase>& asset) override;

    ~LoaderJpeg() override = default;

protected:
    void CreateGpuResource(TSRef<AssetBase>& asset) override;

private:
    struct jpeg_decompress_struct mJpegInfo;
    SizedArray<uint8> mImageData;
};

} // namespace loader

} // namespace fx
