#pragma once

#include "LoaderBase.hpp"

#include <Asset/AssetBase.hpp>
#include <Renderer/Backend/Image.hpp>

namespace fx {

namespace loader {

class ImageLoaderBase : public LoaderBase
{
public:
    eLoaderType GetLoaderType() const override { return eLoaderType::ImageLoader; };

public:
    ImageLoaderBase() = default;

    virtual eLoaderStatus Load(TSRef<AssetBase> asset, const String& path) = 0;
    virtual eLoaderStatus Load(TSRef<AssetBase> asset, const uint8* data, uint32 size) = 0;

    virtual void CreateGpuResource(TSRef<AssetBase>& asset) = 0;

    virtual void Destroy(TSRef<AssetBase>& asset) = 0;

    ~ImageLoaderBase() override = default;

public:
    renderer::eImageType ImageType = renderer::eImageType::Flat;
    eImageFormat ImageFormat = eImageFormat::None;
    eImageCreateFlags CreationFlags = eImageCreateFlags::None;
};

template <typename TLoader>
concept C_IsImageLoader = std::is_base_of_v<ImageLoaderBase, TLoader>;


} // namespace loader

} // namespace fx
