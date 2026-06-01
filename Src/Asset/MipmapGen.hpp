/*
 * File:        MipmapGen.hpp
 * Author:      emd22
 * Created:     18/05/2026 by emd22
 * Description: Generates and loads mipmaps from images
 */

#pragma once

#include <Core/Slice.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Backend/Image.hpp>

namespace fx {

class DataPack;
namespace renderer {
class Image;
}

class MipmapGen
{
public:
    MipmapGen() = default;
    MipmapGen(const char* path, renderer::eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size)
    {
        GenerateMipmaps(path, format, pixels, size);
    }

    void GenerateMipmaps(const char* path, renderer::eImageFormat format, const Slice<uint8>& pixels,
                         const Vec2u& size);

    Slice<uint8> GenerateMip(DataPack& dp, renderer::eImageFormat format, const Slice<uint8>& pixels, const Vec2u& size,
                             uint8 mip_level);

    /**
     * @brief Loads a mipmap datapack and exports each image to its own JPEG.
     * @param dp_path The path to the datapack.
     */
    void ExportMipmaps(const char* dp_path, const char* output_path);

    renderer::Image LoadMipmaps(renderer::CommandBuffer& cmd, const char* path);

    ~MipmapGen() = default;

public:
};

} // namespace fx
