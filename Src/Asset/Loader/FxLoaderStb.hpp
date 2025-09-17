#pragma once

#include "FxLoaderImageBase.hpp"

#include <ThirdParty/stb_image.h>

#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>
#include <string>

class FxLoaderStb : public FxLoaderImageBase
{
public:
    using Status = FxLoaderBase::Status;

    FxLoaderStb() = default;

    Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<FxAssetBase>& asset) override;

    ~FxLoaderStb() override = default;

protected:
    void CreateGpuResource(FxRef<FxAssetBase>& asset) override;

private:
    void LoadCubemapToLayeredImage();

private:
    int mWidth;
    int mHeight;
    int mChannels;

    uint32 mDataSize = 0;
    uint8* mImageData = nullptr;
};
