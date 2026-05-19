#include "MipmapGen.hpp"

#include "DataPack.hpp"

#include <ThirdParty/stb_image_resize2.h>

#include <Asset/Loader/AxLoaderStb.hpp>
#include <Core/FilesystemIO.hpp>

namespace fx {

#pragma pack(push, 1)

struct MipHeader
{
    uint16 SizeX;
    uint16 SizeY;
    uint8 MipLevel;
    uint8 Unused0;
    renderer::eImageFormat Format;
};

#pragma pack(pop)

void MipmapGen::GenerateMipmaps(renderer::eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size)
{
    DataPack dp;
    GenerateMip(dp, format, pixels, size, 0);
    GenerateMip(dp, format, pixels, size, 1);
    GenerateMip(dp, format, pixels, size, 2);
    GenerateMip(dp, format, pixels, size, 4);
    dp.WriteToFile("TestMips.bin");
}

Slice<uint8> MipmapGen::GenerateMip(DataPack& dp, renderer::eImageFormat format, const Slice<uint8>& pixels,
                                    const Vec2u& size, uint8 mip_level)
{
    const uint32 pixel_size = renderer::ImageFormatUtil::GetSize(format);
    static constexpr uint32 scHeaderOffset = sizeof(MipHeader);

    const int32 input_stride = size.X * pixel_size;
    const float32 divisor = 1.0f / float32(mip_level + 1);

    Vec2u output_dimensions(uint32(float32(size.X) * divisor), uint32(float32(size.Y) * divisor));

    const uint32 data_output_size = output_dimensions.X * output_dimensions.Y * pixel_size;

    SizedArray<uint8> output_data;
    output_data.InitSize(data_output_size + scHeaderOffset);

    const int32 output_stride = output_dimensions.X * pixel_size;

    stbir_resize_uint8_linear(pixels.pData, size.X, size.Y, input_stride, output_data.pData + scHeaderOffset,
                              output_dimensions.X, output_dimensions.Y, output_stride, STBIR_RGBA_PM);

    MipHeader header {
        .SizeX = static_cast<uint16>(output_dimensions.X),
        .SizeY = static_cast<uint16>(output_dimensions.Y),
        .MipLevel = static_cast<uint8>(mip_level),
        .Unused0 = 0,
        .Format = format,
    };

    memcpy(output_data.pData, &header, sizeof(header));

#ifdef FX_DEBUG_MIPS_SAVE_AS_IMAGES
    AxLoaderStb::SaveToFile(eImageSaveFormat::Jpeg, output_data, output_dimensions,
                            String::Fmt("Mip_{}.jpeg", mip_level), eImageSaveFlags::None);
#endif

    dp.AddEntry(mip_level, output_data);


    return output_data;
}

void MipmapGen::ExportMipmaps(const char* dp_path, const char* output_path)
{
    FilesystemIO::DirCreate(output_path);

    DataPack dp(eDataPackMode::Read, dp_path);

    if (!dp.IsOpen()) {
        LogError(LC_ASSET, "Failed to open datapack at {} on mipmap export", dp_path);
        return;
    }

    dp.ReadAllEntries();

    for (const DataPackEntry& entry : dp.Entries) {
        MipHeader* header = reinterpret_cast<MipHeader*>(entry.Data.pData);

        Slice<uint8> image_data = Slice(entry.Data.pData + sizeof(MipHeader), entry.Data.Size - sizeof(MipHeader));

        AxLoaderStb::SaveToFile(eImageSaveFormat::Jpeg, image_data, Vec2u(header->SizeX, header->SizeY),
                                String::Fmt("{}/Mip{}.jpeg", output_path, header->MipLevel), eImageSaveFlags::None);
    }

    dp.Close();
}


} // namespace fx
