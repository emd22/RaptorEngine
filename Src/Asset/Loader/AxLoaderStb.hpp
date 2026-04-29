#pragma once

#include "AxLoaderImageBase.hpp"

#include <ThirdParty/stb_image.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>

namespace fx {


class AxLoaderStb : public AxLoaderImageBase
{
public:
    using eStatus = AxLoaderBase::eStatus;

    AxLoaderStb() = default;

    eStatus LoadFromFile(TSRef<AxBase> asset, const String& path) override;
    eStatus LoadFromMemory(TSRef<AxBase> asset, const uint8* data, uint32 size) override;

    static eStatus SaveToFile(eImageSaveFormat format, const SizedArray<uint8>& data, const Vec2u& size,
                              const String& path, eImageSaveFlags flags);

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
