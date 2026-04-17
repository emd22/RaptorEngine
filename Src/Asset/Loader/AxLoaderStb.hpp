#pragma once

#include "AxLoaderImageBase.hpp"

#include <ThirdParty/stb_image.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <string>

namespace fx {


class AxLoaderStb : public AxLoaderImageBase
{
public:
    using Status = AxLoaderBase::eStatus;

    AxLoaderStb() = default;

    Status LoadFromFile(TSRef<AxBase> asset, const std::string& path) override;
    Status LoadFromMemory(TSRef<AxBase> asset, const uint8* data, uint32 size) override;

    void Destroy(TSRef<AxBase>& asset) override;

    ~AxLoaderStb() override = default;

protected:
    void CreateGpuResource(TSRef<AxBase>& asset) override;

private:
    void LoadCubemapToLayeredImage();

private:
    int mWidth;
    int mHeight;
    int mChannels;

    uint32 mDataSize = 0;
    uint8* mImageData = nullptr;
};

} // namespace fx
