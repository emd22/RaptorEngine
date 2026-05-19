#include "MipmapFile.hpp"

#include "DataPack.hpp"

#include <ThirdParty/stb_image_resize2.h>

#include <Asset/Loader/AxLoaderStb.hpp>


namespace fx {

void MipmapFile::GenerateMips(const Slice<uint8>& pixels, const Vec2u& size)
{
    DataPack dp;
    GenerateMip(dp, pixels, size, 0);
    GenerateMip(dp, pixels, size, 1);
    GenerateMip(dp, pixels, size, 2);
    GenerateMip(dp, pixels, size, 4);
    dp.WriteToFile("TestMips.bin");
}

Slice<uint8> MipmapFile::GenerateMip(DataPack& dp, const Slice<uint8>& pixels, const Vec2u& size, uint32 mip_level)
{
    static constexpr uint32 scPixelSize = 4;

    int32 stride = size.X * scPixelSize;
    const float32 divisor = 1.0f / float32(mip_level + 1);

    Vec2u output_dimensions(uint32(float32(size.X) * divisor), uint32(float32(size.Y) * divisor));

    const uint32 output_size = output_dimensions.X * output_dimensions.Y * scPixelSize;
    SizedArray<uint8> output_data;
    output_data.InitSize(output_size);

    int32 output_stride = output_dimensions.X * scPixelSize;


    stbir_resize_uint8_linear(pixels.pData, size.X, size.Y, stride, output_data.pData, output_dimensions.X,
                              output_dimensions.Y, output_stride, STBIR_RGBA_PM);

#ifdef FX_DEBUG_MIPS_SAVE_AS_IMAGES
    AxLoaderStb::SaveToFile(eImageSaveFormat::Jpeg, output_data, output_dimensions,
                            String::Fmt("Mip_{}.jpeg", mip_level), eImageSaveFlags::None);
#endif

    dp.AddEntry(mip_level, output_data);


    return output_data;
}

} // namespace fx
