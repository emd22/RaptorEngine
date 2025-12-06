#pragma once

#include "AxLoaderImageBase.hpp"

#include <jpeglib.h>

#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>
#include <string>

class AxLoaderJpeg : public AxLoaderImageBase
{
public:
    using Status = AxLoaderBase::Status;

    AxLoaderJpeg() = default;

    Status LoadFromFile(FxRef<AxBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<AxBase> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<AxBase>& asset) override;

    ~AxLoaderJpeg() override = default;

protected:
    void CreateGpuResource(FxRef<AxBase>& asset) override;

private:
    struct jpeg_decompress_struct mJpegInfo;
    FxSizedArray<uint8> mImageData;
};
