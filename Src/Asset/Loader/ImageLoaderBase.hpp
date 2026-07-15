#pragma once

#include "LoaderBase.hpp"

#include <Asset/AssetTicket.hpp>
#include <Renderer/Backend/Image.hpp>

namespace fx {

namespace loader {

class ImageLoaderBase : public LoaderBase
{
public:
	eLoaderType GetLoaderType() const override { return eLoaderType::ImageLoader; };

public:
	ImageLoaderBase() = default;

	virtual eLoaderStatus Load(AssetTicket& ticket, const std::string& path) = 0;
	virtual eLoaderStatus Load(AssetTicket& ticket, const uint8* data, uint32 size) = 0;

	virtual void CreateGpuResource(AssetTicket& ticket) = 0;

	virtual void Destroy() = 0;

	~ImageLoaderBase() override = default;

public:
	eImageType ImageType = eImageType::Flat;
	eImageFormat ImageFormat = eImageFormat::None;
	eImageCreateFlags CreationFlags = eImageCreateFlags::None;
};

template <typename TLoader>
concept C_IsImageLoader = std::is_base_of_v<ImageLoaderBase, TLoader>;


} // namespace loader

} // namespace fx
