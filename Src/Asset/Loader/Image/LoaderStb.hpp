#pragma once

#include "../ImageLoaderBase.hpp"

#include <ThirdParty/stb_image.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>

namespace fx {

namespace loader {

class LoaderStb final : public ImageLoaderBase
{
public:
	LoaderStb() = default;

	eLoaderStatus Load(AssetTicket& asset, const std::string& path) override;
	eLoaderStatus Load(AssetTicket& asset, const uint8* data, uint32 size) override;

	void CreateGpuResource(AssetTicket& asset) override;

	static eLoaderStatus SaveToFile(eImageSaveFormat format, const Slice<uint8>& data, const Vec2u& size,
									const String& path, eImageSaveFlags flags);

	Slice<uint8> GetImageData() const { return Slice(mImageData, mDataSize); }
	Vec2u GetImageSize() const { return Vec2u(mWidth, mHeight); };

	void Destroy() override;
	void InvalidateImageData() { mImageData = nullptr; };

	~LoaderStb() override = default;

private:
	void LoadCubemapToLayeredImage();

private:
	int mWidth = 0;
	int mHeight = 0;
	int mChannels = 0;

	uint32 mDataSize = 0;
	uint8* mImageData = nullptr;
};

} // namespace loader

} // namespace fx
