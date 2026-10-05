#pragma once

#include "../ImageLoaderBase.hpp"

#include <raptor_ffi.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>

namespace fx {

namespace loader {

/**
 * @brief Loads PNG, JPEG, BMP, TGA and GIF images with 8 bits to a channel, to the number of channels of the image
 * format.
 */
class LoaderStb final : public ImageLoaderBase
{
public:
	LoaderStb() = default;
	LoaderStb(const LoaderStb&) = delete;
	LoaderStb& operator=(const LoaderStb&) = delete;

	eLoaderStatus Load(AssetTicket& asset, const std::string& path) override;
	eLoaderStatus Load(AssetTicket& asset, const uint8* data, uint32 size) override;

	void CreateGpuResource(AssetTicket& asset) override;

	static eLoaderStatus SaveToFile(eImageSaveFormat format, const Slice<const uint8>& data, const Vec2u& size,
									const String& path, eImageSaveFlags flags);

	Slice<uint8> GetImageData() const;
	Vec2u GetImageSize() const { return Vec2u(mWidth, mHeight); };

	void Destroy() override;

	~LoaderStb() override { Destroy(); }

private:
	bool Adopt(RxDecodedImage* image);

private:
	uint32 mWidth = 0;
	uint32 mHeight = 0;

	uint32 mDataSize = 0;
	RxDecodedImage* mpImage = nullptr;
};

} // namespace loader

} // namespace fx
