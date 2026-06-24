#pragma once

#include "../AxLoaderImageBase.hpp"

#include <TurboJPEG/jpeglib.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <string>

namespace fx {

namespace loader {

class AxLoaderJpeg : public AxLoaderImageBase
{
public:
    using Status = AxLoaderBase::eStatus;

    AxLoaderJpeg() = default;

    Status LoadFromFile(TSRef<AxBase> asset, const String& path) override;
    Status LoadFromMemory(TSRef<AxBase> asset, const uint8* data, uint32 size) override;

    void Destroy(TSRef<AxBase>& asset) override;

    ~AxLoaderJpeg() override = default;

protected:
    void CreateGpuResource(TSRef<AxBase>& asset) override;

private:
    struct jpeg_decompress_struct mJpegInfo;
    SizedArray<uint8> mImageData;
};

} // namespace loader

} // namespace fx
