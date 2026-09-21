/*
 * File:        MipmapGen.hpp
 * Author:      emd22
 * Created:     18/05/2026 by emd22
 * Description: Generates and loads mipmaps from images
 */

#pragma once

#include <Asset/Loader/Image/LoaderKtx.hpp>
#include <Core/Slice.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Backend/Image.hpp>

namespace fx {

class Image;

enum class eQualityLevel
{
	LowQuality,
	HighQuality,
};

class MipmapGen
{
public:
	MipmapGen() = default;
	MipmapGen(const char* path, eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size)
	{
		GenerateMipmaps(path, format, pixels, size);
	}

	uint32 GetExpectedMipCount(Vec2u base_size);

	void GenerateMipmaps(const char* path, eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size);

	/// Generates the pixels of a single mip level, tightly packed.
	SizedArray<uint8> GenerateMip(eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size,
								  uint8 mip_level);

	void GenerateTestMipmap(const char* output_path, const Vec2u& size);

	/**
	 * @brief Loads a mipmap KTX file and exports each mip level to its own single-level KTX file.
	 * @param ktx_path The path to the mipmapped KTX file.
	 */
	void ExportMipmaps(const char* ktx_path, const char* output_path);

	Image LoadMipmaps(renderer::CommandBuffer& cmd, const char* path);

	~MipmapGen() = default;

public:
};

class MipmapLoader
{
public:
	MipmapLoader() = default;

	void Open(const char* path);

	bool IsOpen() const { return Ktx.IsOpen(); }

	uint32 GetMipCount() const { return Ktx.IsOpen() ? Ktx.GetMipCount() : 0; }

	ImageInfo GetMip(uint32 mip_level);

	ImageInfo GetQuality(eQualityLevel quality);

public:
	loader::LoaderKtx Ktx;
};


} // namespace fx
