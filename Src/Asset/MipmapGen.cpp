#include "MipmapGen.hpp"

#include "DataPack.hpp"

#include <ThirdParty/stb_image_resize2.h>

#include <Asset/Loader/Image/LoaderStb.hpp>
#include <Core/ArrayUtil.hpp>
#include <Core/FilesystemIO.hpp>
#include <Renderer/Backend/Image.hpp>


namespace fx {

#pragma pack(push, 1)

struct MipHeader
{
	uint16 SizeX;
	uint16 SizeY;
	uint8 MipLevel;
	uint8 Unused0;
	eImageFormat Format;
};

#pragma pack(pop)

void MipmapGen::GenerateMipmaps(const char* path, eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size)
{
	DataPack dp;
	GenerateMip(dp, format, pixels, size, 0);
	GenerateMip(dp, format, pixels, size, 1);
	GenerateMip(dp, format, pixels, size, 2);
	GenerateMip(dp, format, pixels, size, 3);
	dp.WriteToFile(path);
}

FX_FORCE_INLINE constexpr float32 GetMipDivisor(uint32 mip_level)
{
	return 1.0f / (1U << static_cast<uint32>(mip_level));
}

FX_FORCE_INLINE constexpr float32 GetBaseMipMultiplier(uint32 mip_level)
{
	return (1U << static_cast<uint32>(mip_level));
}

uint32 MipmapGen::CalculateExpectedMipCount(Vec2u base_size) { return 0; }


Slice<uint8> MipmapGen::GenerateMip(DataPack& dp, eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size,
									uint8 mip_level)
{
	static constexpr uint32 scHeaderOffset = sizeof(MipHeader);

	const uint32 pixel_size = renderer::ImageFormatUtil::GetPixelStride(format);

	const int32 input_stride = size.X * pixel_size;
	const float32 divisor = GetMipDivisor(static_cast<uint32>(mip_level));

	Vec2u output_dimensions(std::max(1U, uint32(float32(size.X) * divisor)),
							std::max(1U, uint32(float32(size.Y) * divisor)));

	SizedArray<uint8> output_data;

	{
		const uint32 data_output_size = output_dimensions.X * output_dimensions.Y * pixel_size;
		output_data.InitSize(data_output_size + scHeaderOffset);

		const int32 output_stride = output_dimensions.X * pixel_size;

		if (mip_level > 0) {
			stbir_resize_uint8_linear(pixels.pData, size.X, size.Y, input_stride, output_data.pData + scHeaderOffset,
									  output_dimensions.X, output_dimensions.Y, output_stride, STBIR_RGBA_PM);
		}
		else {
			Assert(output_dimensions == size);
			memcpy(output_data.pData + scHeaderOffset, pixels.pData, data_output_size);
		}
	}

	// Write header
	{
		MipHeader header {
			.SizeX = static_cast<uint16>(output_dimensions.X),
			.SizeY = static_cast<uint16>(output_dimensions.Y),
			.MipLevel = static_cast<uint8>(mip_level),
			.Unused0 = 0,
			.Format = format,
		};

		memcpy(output_data.pData, &header, sizeof(header));
	}

#ifdef FX_DEBUG_MIPS_SAVE_AS_IMAGES
	loader::LoaderStb::SaveToFile(eImageSaveFormat::Jpeg, output_data, output_dimensions,
								  String::Fmt("Mip_{}.jpeg", mip_level), eImageSaveFlags::None);
#endif

	dp.AddEntry(mip_level, output_data);

	return Slice(dp.GetEntry(mip_level, false)->Data);
}

void MipmapGen::GenerateTestMipmap(const char* output_path, const Vec2u& size)
{
	DataPack dp;

	static constexpr uint32 scHeaderOffset = sizeof(MipHeader);
	static constexpr uint32 scNumMips = 4;
	static constexpr uint32 scPixelStride = 4;

	// The buffer is the size of mip level 0, where each mip will get progressively smaller.
	const uint32 buffer_size = (size.X * size.Y * scPixelStride) + scHeaderOffset;
	uint8* buffer = static_cast<uint8*>(std::malloc(buffer_size));

	const uint8 mip_level_colors[][4] = {
		{ 0xFF, 0x11, 0x11, 0xFF }, // Mip Level 0 : Red
		{ 0xFF, 0xFF, 0x11, 0xFF }, // Mip Level 1 : Yellow
		{ 0x11, 0xFF, 0x11, 0xFF }, // Mip Level 2 : Green
		{ 0x11, 0xFF, 0xFF, 0xFF }, // Mip Level 3 : Teal
	};

	for (uint32 mip_level = 0; mip_level < scNumMips; mip_level++) {
		const uint8* level_color = mip_level_colors[(mip_level % std::size(mip_level_colors))];
		const float32 mip_divisor = GetMipDivisor(mip_level);

		const uint32 mip_buffer_size = (size.X * mip_divisor) * (size.Y * mip_divisor);

		// Write the image data
		for (uint32 b_index = 0; b_index < mip_buffer_size; b_index++) {
			memcpy(buffer + scHeaderOffset + (b_index * scPixelStride), level_color, scPixelStride);
		}

		// Write the mip header
		{
			MipHeader header {
				.SizeX = static_cast<uint16>(size.X * mip_divisor),
				.SizeY = static_cast<uint16>(size.Y * mip_divisor),
				.MipLevel = static_cast<uint8>(mip_level),
				.Unused0 = 0,
				.Format = eImageFormat::RGBA8_UNorm,
			};

			LogInfo("Mippy Width: {}, Height: {}", header.SizeX, header.SizeY);

			memcpy(buffer, &header, sizeof(header));
		}

		dp.AddEntry(mip_level, Slice<uint8>(buffer, mip_buffer_size * scPixelStride + scHeaderOffset));
	}

	dp.WriteToFile(output_path);

	std::free(buffer);
}


void MipmapGen::ExportMipmaps(const char* dp_path, const char* output_path)
{
	FilesystemIO::DirCreate(output_path);

	DataPack dp(eDataPackMode::Read, dp_path);

	if (!dp.IsOpen()) {
		LogError(LC_ASSET, "Failed to open datapack at {} on mipmap export", dp_path);
		return;
	}

	for (DataPackEntry& entry : dp.Entries) {
		// If there is no data in this datapack entry, load it from the file.
		if (!entry.HasData()) {
			dp.ReadInto(&entry);
		}

		MipHeader* header = reinterpret_cast<MipHeader*>(entry.Data.pData);

		Slice<uint8> image_data = Slice(entry.Data.pData + sizeof(MipHeader), entry.Data.Size - sizeof(MipHeader));

		loader::LoaderStb::SaveToFile(eImageSaveFormat::Jpeg, image_data, Vec2u(header->SizeX, header->SizeY),
									  String::Fmt("{}/Mip{}.jpeg", output_path, header->MipLevel),
									  eImageSaveFlags::None);
	}

	dp.Close();
}

#define M_HEADER_PTR(ptr_) reinterpret_cast<MipHeader*>(ptr_)
#define M_DATA_PTR(ptr_)   (ptr_ + sizeof(MipHeader))
#define M_DATA_SIZE(arr_)  (arr_.Size - sizeof(MipHeader))

renderer::Image MipmapGen::LoadMipmaps(renderer::CommandBuffer& cmd, const char* path)
{
	renderer::Image image;

	DataPack dp;
	dp.ReadFromFile(path);

	if (!dp.IsOpen()) {
		return image;
	}

	const int32 num_mips = static_cast<int32>(dp.Entries.Size());

	DataPackEntry* base_mip = &dp.Entries[0];
	dp.ReadInto(base_mip);

	MipHeader* base_header = M_HEADER_PTR(base_mip->Data.pData);
	uint8* base_data = M_DATA_PTR(base_mip->Data.pData);
	uint32 base_data_size = M_DATA_SIZE(base_mip->Data);

	const Vec2u image_size(base_header->SizeX, base_header->SizeY);
	Slice<uint8> image_data(base_data, base_data_size);

	ImageInfo image_info {
		image_size,						  // Dimensions
		eImageFormat::RGBA8_UNorm,		  // Format
		static_cast<int32>(base_mip->Id), // Mip level
		num_mips,						  // Mips count
		image_data,						  // Data
	};

	image.CreateFromData(cmd, image_info, eImageCreateFlags::None);

	// for (uint32 i = 1; i < num_mips; i++) {
	// 	DataPackEntry* entry = dp.GetEntry(i, true);

	// 	MipHeader* header = M_HEADER_PTR(base_mip->Data.pData);
	// 	uint8* data = M_DATA_PTR(base_mip->Data.pData);
	// 	uint32 data_size = M_DATA_SIZE(base_mip->Data);

	// 	LogInfo("Loading mip {} with size {}x{}", header->MipLevel, header->SizeX, header->SizeY);

	// 	image.UploadMip(cmd, header->MipLevel, Vec2u(header->SizeX, header->SizeY), MakeSlice<uint8>(data, data_size));
	// }

	return image;
}


/////////////////////////////////////
// Mipmap Loader
/////////////////////////////////////


void MipmapLoader::Open(const char* path)
{
	Pack.ReadFromFile(path);
	if (!Pack.IsOpen()) {
		LogError(LC_ASSET, "MipmapLoader: Could not open datapack");
		return;
	}
}

ImageInfo MipmapLoader::GetMip(uint32 mip_level)
{
	mip_level = FindClosestMipLevel(mip_level);

	DataPackEntry* mip_entry = Pack.GetEntry(mip_level, true);

	LogInfo("Mip (DataOffset={}, DataSize={})", mip_entry->DataOffset, mip_entry->DataSize);

	MipHeader* header = M_HEADER_PTR(mip_entry->Data.pData);
	uint8* image_data = M_DATA_PTR(mip_entry->Data.pData);
	uint32 image_size = M_DATA_SIZE(mip_entry->Data);

	// When we upload the image, we want the image to be created to be the size of Mip 0.
	// float32 bmm = GetBaseMipMultiplier(mip_level);

	float32 bmm = 1.0f;

	ImageInfo image_info {};
	image_info.MipLevel = 0;
	image_info.MipCount = 1;
	image_info.Format = header->Format;
	image_info.Size = Vec2u(static_cast<uint32>(static_cast<float32>(header->SizeX) * bmm),
							static_cast<uint32>(static_cast<float32>(header->SizeY) * bmm));

	LogInfo("Image info size: {}x{}", header->SizeX, header->SizeY);

	image_info.ImageData = ArrayUtil::SliceDupe(Slice<const uint8>(image_data, image_size));

	return image_info;
}


uint32 MipmapLoader::FindClosestMipLevel(uint32 mip_level)
{
	DataPackEntry* prev = nullptr;

	for (DataPackEntry& entry : Pack.Entries) {
		// If the (next) entry's mip level is greater than the desired mip level, return the previous one.
		if (entry.Id > mip_level) {
			LogInfo("Closest mip level is {} (for {})", entry.Id, mip_level);

			return prev->Id;
		}

		prev = &entry;
	}

	// No mips in the pack...
	if (!prev) {
		return 0;
	}

	return prev->Id;
}


} // namespace fx
