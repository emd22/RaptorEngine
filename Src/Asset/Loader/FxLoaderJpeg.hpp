#pragma once

#include "FxLoaderImageBase.hpp"

#include <TurboJPEG/jpeglib.h>

#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>
#include <string>

class FxLoaderJpeg : public FxLoaderImageBase
{
public:
    using Status = FxLoaderBase::Status;

    FxLoaderJpeg() = default;

    Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<FxAssetBase>& asset) override;

    ~FxLoaderJpeg() override = default;

protected:
    void CreateGpuResource(FxRef<FxAssetBase>& asset) override;

private:
    struct jpeg_decompress_struct mJpegInfo;
    FxSizedArray<uint8> mImageData;
};
