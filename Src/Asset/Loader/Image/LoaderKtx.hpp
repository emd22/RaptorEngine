#pragma once

#include "../ImageLoaderBase.hpp"

#include <ktx.h>

#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>

namespace fx {

namespace loader {

/**
 * @brief Loads and saves KTX (v1 and v2) images, including full mip chains.
 *
 * The pixel format is read from the file, so `ImageFormat` is ignored when loading.
 */
class LoaderKtx final : public ImageLoaderBase
{
public:
	static constexpr const char* scFileExtension = ".ktx2";

public:
	LoaderKtx() = default;
	LoaderKtx(const LoaderKtx&) = delete;
	LoaderKtx& operator=(const LoaderKtx&) = delete;

	eLoaderStatus Load(AssetTicket& ticket, const std::string& path) override;
	eLoaderStatus Load(AssetTicket& ticket, const uint8* data, uint32 size) override;

	/// Loads a KTX file without an asset ticket. Returns whether the file was loaded.
	bool Open(const char* path);

	void CreateGpuResource(AssetTicket& ticket) override;

	/**
	 * @brief Writes a KTX2 file from a set of tightly packed mip levels.
	 * @param mips The pixel data of each mip level, ordered from the base (largest) level down.
	 * @param size The dimensions of the base level. Level N is expected to be max(1, size >> N).
	 */
	static eLoaderStatus SaveToFile(const String& path, eImageFormat format, const Vec2u& size,
									const Slice<const Slice<const uint8>>& mips);

	bool IsOpen() const { return mpTexture != nullptr; }

	eImageFormat GetFormat() const { return mFormat; }
	Vec2u GetImageSize() const { return Vec2u(mpTexture->baseWidth, mpTexture->baseHeight); }
	uint32 GetMipCount() const { return mpTexture->numLevels; }

	Vec2u GetMipDimensions(uint32 mip_level) const;

	/// The pixel data of a mip level. Points into the loader's memory and is valid until Destroy().
	Slice<const uint8> GetMipData(uint32 mip_level) const;

	/**
	 * @brief Builds an ImageInfo from `mip_count` levels starting at `first_mip` (0 = through the smallest level).
	 * `ImageData` is a malloc'd copy of the levels packed tightly one after another, owned by the caller.
	 */
	ImageInfo MakeImageInfo(uint32 first_mip = 0, uint32 mip_count = 0) const;

	void Destroy() override;

	~LoaderKtx() override { Destroy(); }

private:
	bool AdoptTexture(ktxTexture* texture);

private:
	ktxTexture* mpTexture = nullptr;
	eImageFormat mFormat = eImageFormat::None;
};

} // namespace loader

} // namespace fx
