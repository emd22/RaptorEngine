#include "MipmapGen.hpp"

#include <ThirdParty/stb_image_resize2.h>

#include <Asset/Loader/Image/LoaderKtx.hpp>
#include <Asset/Loader/Image/LoaderStb.hpp>
#include <Core/FilesystemIO.hpp>
#include <Renderer/Backend/Image.hpp>
#include <algorithm>
#include <vector>


namespace fx {

void MipmapGen::GenerateMipmaps(const char* path, eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size)
{
	const uint32 expected_mip_count = GetExpectedMipCount(size);

	std::vector<SizedArray<uint8>> mips;
	std::vector<Slice<const uint8>> mip_slices;

	mips.reserve(expected_mip_count);
	mip_slices.reserve(expected_mip_count);

	for (uint32 i = 0; i < expected_mip_count; i++) {
		mips.push_back(GenerateMip(format, pixels, size, i));
		mip_slices.emplace_back(mips.back().pData, mips.back().Size);
	}

	loader::LoaderKtx::SaveToFile(path, format, size,
								  Slice<const Slice<const uint8>>(mip_slices.data(), mip_slices.size()));
}

uint32 MipmapGen::GetExpectedMipCount(Vec2u base_size)
{
	return static_cast<uint32_t>(std::floor(std::log2(std::max(base_size.X, base_size.Y)))) + 1U;
}


SizedArray<uint8> MipmapGen::GenerateMip(eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size,
										 uint8 mip_level)
{
	const uint32 pixel_size = ImageFormatUtil::GetPixelStride(format);

	const int32 input_stride = size.X * pixel_size;

	// Matches the level dimensions KTX derives from the base size.
	Vec2u output_dimensions(std::max(1U, size.X >> mip_level), std::max(1U, size.Y >> mip_level));

	SizedArray<uint8> output_data;

	const uint32 data_output_size = output_dimensions.X * output_dimensions.Y * pixel_size;
	output_data.InitSize(data_output_size);

	const int32 output_stride = output_dimensions.X * pixel_size;

	if (mip_level > 0) {
		// STBIR_RGBA, not STBIR_RGBA_PM: these textures carry straight alpha, and claiming they are premultiplied
		// lets the colour of fully transparent texels bleed into the chain (dark fringes around cutouts).
		if (ImageFormatUtil::IsSrgb(format)) {
			// Averaging sRGB encoded bytes as if they were linear darkens every level below the base, so the
			// colour channels are filtered through the transfer function. Alpha stays linear either way.
			stbir_resize_uint8_srgb(pixels.pData, size.X, size.Y, input_stride, output_data.pData, output_dimensions.X,
									output_dimensions.Y, output_stride, STBIR_RGBA);
		}
		else {
			stbir_resize_uint8_linear(pixels.pData, size.X, size.Y, input_stride, output_data.pData,
									  output_dimensions.X, output_dimensions.Y, output_stride, STBIR_RGBA);
		}
	}
	else {
		Assert(output_dimensions == size);
		memcpy(output_data.pData, pixels.pData, data_output_size);
	}

#ifdef FX_DEBUG_MIPS_SAVE_AS_IMAGES
	loader::LoaderStb::SaveToFile(eImageSaveFormat::Jpeg, output_data, output_dimensions,
								  String::Fmt("Mip_{}.jpeg", mip_level), eImageSaveFlags::None);
#endif

	return output_data;
}

void MipmapGen::GenerateTestMipmap(const char* output_path, const Vec2u& size)
{
	static constexpr uint32 scNumMips = 4;
	static constexpr uint32 scPixelStride = 4;

	const uint8 mip_level_colors[][4] = {
		{ 0xFF, 0x11, 0x11, 0xFF }, // Mip Level 0 : Red
		{ 0xFF, 0xFF, 0x11, 0xFF }, // Mip Level 1 : Yellow
		{ 0x11, 0xFF, 0x11, 0xFF }, // Mip Level 2 : Green
		{ 0x11, 0xFF, 0xFF, 0xFF }, // Mip Level 3 : Teal
	};

	std::vector<std::vector<uint8>> mips(scNumMips);
	std::vector<Slice<const uint8>> mip_slices;

	for (uint32 mip_level = 0; mip_level < scNumMips; mip_level++) {
		const uint8* level_color = mip_level_colors[(mip_level % std::size(mip_level_colors))];

		const Vec2u mip_size(std::max(1U, size.X >> mip_level), std::max(1U, size.Y >> mip_level));
		const uint32 mip_pixel_count = mip_size.X * mip_size.Y;

		mips[mip_level].resize(mip_pixel_count * scPixelStride);

		for (uint32 pixel_index = 0; pixel_index < mip_pixel_count; pixel_index++) {
			memcpy(mips[mip_level].data() + (pixel_index * scPixelStride), level_color, scPixelStride);
		}


		mip_slices.emplace_back(mips[mip_level].data(), static_cast<uint32>(mips[mip_level].size()));
	}

	loader::LoaderKtx::SaveToFile(output_path, eImageFormat::RGBA8_UNorm, size,
								  Slice<const Slice<const uint8>>(mip_slices.data(), mip_slices.size()));
}


void MipmapGen::ExportMipmaps(const char* ktx_path, const char* output_path)
{
	FilesystemIO::DirCreate(output_path);

	loader::LoaderKtx ktx;

	if (!ktx.Open(ktx_path)) {
		LogError(LC_ASSET, "Failed to open KTX file at {} on mipmap export", ktx_path);
		return;
	}

	for (uint32 mip_level = 0; mip_level < ktx.GetMipCount(); mip_level++) {
		const Slice<const uint8> mip_data = ktx.GetMipData(mip_level);

		loader::LoaderKtx::SaveToFile(
			String::Fmt("{}/Mip{}{}", output_path, mip_level, loader::LoaderKtx::scFileExtension), ktx.GetFormat(),
			ktx.GetMipDimensions(mip_level), Slice<const Slice<const uint8>>(&mip_data, 1));
	}
}

Image MipmapGen::LoadMipmaps(renderer::CommandBuffer& cmd, const char* path)
{
	Image image;

	loader::LoaderKtx ktx;
	if (!ktx.Open(path)) {
		return image;
	}

	ImageInfo image_info = ktx.MakeImageInfo(0);

	image.Upload(cmd, image_info);

	image_info.FreeOwnedData();

	return image;
}


/////////////////////////////////////
// Mipmap Loader
/////////////////////////////////////


void MipmapLoader::Open(const char* path)
{
	if (!Ktx.Open(path)) {
		LogError(LC_ASSET, "MipmapLoader: Could not open KTX file '{}'", path);
	}
}

ImageInfo MipmapLoader::GetMip(uint32 mip_level)
{
	Assert(Ktx.IsOpen());

	mip_level = std::min(mip_level, Ktx.GetMipCount() - 1);

	// Each mip is returned as its own single-level image.
	return Ktx.MakeImageInfo(mip_level, 1);
}

ImageInfo MipmapLoader::GetQuality(eQualityLevel quality)
{
	Assert(Ktx.IsOpen());

	uint32 zero_level = 0;
	if (quality == eQualityLevel::LowQuality) {
		zero_level = Ktx.GetMipCount() / 2;
	}

	return Ktx.MakeImageInfo(zero_level);
}


} // namespace fx
