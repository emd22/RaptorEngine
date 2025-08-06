#pragma once

#include <string>

#include <Core/Types.hpp>
#include <Core/FxSizedArray.hpp>
#include "FxLoaderBase.hpp"

#include <ThirdParty/stb_image.h>

class FxLoaderStb : public FxLoaderBase
{
public:
    using Status = FxLoaderBase::Status;

    FxLoaderStb() = default;

    Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<FxAssetBase>& asset) override;

    ~FxLoaderStb() = default;

protected:
    void CreateGpuResource(FxRef<FxAssetBase>& asset) override;

private:
    int mWidth;
    int mHeight;
    int mChannels;

    uint32 mDataSize = 0;
    uint8* mImageData = nullptr;
};
