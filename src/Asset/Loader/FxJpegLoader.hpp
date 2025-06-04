#pragma once

#include <string>

#include <Core/Types.hpp>
#include "FxBaseLoader.hpp"

#include <jpeglib.h>

class FxJpegLoader : public FxBaseLoader
{
public:
    using Status = FxBaseLoader::Status;

    FxJpegLoader() = default;

    Status LoadFromFile(FxBaseAsset *asset, std::string path) override;
    void Destroy(FxBaseAsset *asset) override;

    ~FxJpegLoader() = default;

protected:
    void CreateGpuResource(FxBaseAsset *asset) override;

private:
    struct jpeg_decompress_struct mJpegInfo;
    uint8* mImageData = nullptr;
};
