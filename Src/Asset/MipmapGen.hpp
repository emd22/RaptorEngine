/*
 * File:        MipmapGen.hpp
 * Author:      emd22
 * Created:     18/05/2026 by emd22
 * Description: Generates and loads mipmaps from images
 */

#pragma once

#include <Asset/DataPack.hpp>
#include <Core/Slice.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Backend/Image.hpp>

namespace fx {

namespace renderer {
class Image;
}

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

	Slice<uint8> GenerateMip(DataPack& dp, eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size,
							 uint8 mip_level);

	void GenerateTestMipmap(const char* output_path, const Vec2u& size);

	/**
	 * @brief Loads a mipmap datapack and exports each image to its own JPEG.
	 * @param dp_path The path to the datapack.
	 */
	void ExportMipmaps(const char* dp_path, const char* output_path);

	renderer::Image LoadMipmaps(renderer::CommandBuffer& cmd, const char* path);

	~MipmapGen() = default;

public:
};

class MipmapLoader
{
public:
	MipmapLoader() = default;

	void Open(const char* path);

	ImageInfo GetMip(uint32 mip_level);

	ImageInfo GetLowQuality();

private:
	uint32 FindClosestMipLevel(uint32 mip_level);

public:
	DataPack Pack;
};


} // namespace fx
