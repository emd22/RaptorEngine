#pragma once

#include <Core/Slice.hpp>
#include <Math/Vec2.hpp>

namespace fx {

class DataPack;

class MipmapFile
{
public:
    MipmapFile() = default;

    void GenerateMips(const Slice<uint8>& pixels, const Vec2u& size);
    Slice<uint8> GenerateMip(DataPack& dp, const Slice<uint8>& pixels, const Vec2u& size, uint32 mip_level);

    ~MipmapFile() = default;

public:
};

} // namespace fx
