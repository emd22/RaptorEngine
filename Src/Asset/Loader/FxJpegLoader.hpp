#pragma once

#include <string>

#include <Core/Types.hpp>
#include <Core/FxSizedArray.hpp>
#include "FxBaseLoader.hpp"

#include <jpeglib.h>

class FxJpegLoader : public FxBaseLoader
{
public:
    using Status = FxBaseLoader::Status;

    FxJpegLoader() = default;

    Status LoadFromFile(FxRef<FxBaseAsset> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxBaseAsset> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<FxBaseAsset>& asset) override;

    ~FxJpegLoader() = default;

protected:
    void CreateGpuResource(FxRef<FxBaseAsset>& asset) override;

private:
    struct jpeg_decompress_struct mJpegInfo;
    FxSizedArray<uint8> mImageData;
};
