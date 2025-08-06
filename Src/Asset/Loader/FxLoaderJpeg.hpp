#pragma once

#include <string>

#include <Core/Types.hpp>
#include <Core/FxSizedArray.hpp>
#include "FxLoaderBase.hpp"

#include <jpeglib.h>

class FxLoaderJpeg : public FxLoaderBase
{
public:
    using Status = FxLoaderBase::Status;

    FxLoaderJpeg() = default;

    Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<FxAssetBase>& asset) override;

    ~FxLoaderJpeg() = default;

protected:
    void CreateGpuResource(FxRef<FxAssetBase>& asset) override;

private:
    struct jpeg_decompress_struct mJpegInfo;
    FxSizedArray<uint8> mImageData;
};
