#pragma once

#include "../ImageLoaderBase.hpp"

#include <TurboJPEG/jpeglib.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>

namespace fx {

namespace loader {

class LoaderJpeg : public ImageLoaderBase
{
public:
	LoaderJpeg() = default;

	eLoaderStatus Load(AssetTicket& ticket, const std::string& path) override;
	eLoaderStatus Load(AssetTicket& ticket, const uint8* data, uint32 size) override;

	void CreateGpuResource(AssetTicket& ticket) override;

	void Destroy() override;

	~LoaderJpeg() override = default;

private:
	struct jpeg_decompress_struct mJpegInfo;
	SizedArray<uint8> mImageData;
};

} // namespace loader

} // namespace fx
