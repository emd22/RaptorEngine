#pragma once

#include <string>

#include <Core/Types.hpp>
#include <Core/FxStaticArray.hpp>
#include "FxBaseLoader.hpp"

#include <jpeglib.h>

class FxJpegLoader : public FxBaseLoader
{
public:
    using Status = FxBaseLoader::Status;

    FxJpegLoader() = default;

    Status LoadFromFile(std::shared_ptr<FxBaseAsset>& asset, const std::string& path) override;
    void Destroy(std::shared_ptr<FxBaseAsset>& asset) override;

    ~FxJpegLoader() = default;

protected:
    void CreateGpuResource(std::shared_ptr<FxBaseAsset>& asset) override;

private:
    struct jpeg_decompress_struct mJpegInfo;
    FxStaticArray<uint8> mImageData;
};
