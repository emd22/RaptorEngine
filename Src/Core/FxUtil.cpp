#include "FxUtil.hpp"

uint32 FxColorFromRGBA(uint8 r, uint8 g, uint8 b, uint8 a)
{
    return ((static_cast<uint32>(a) << 24) | (static_cast<uint32>(b) << 16) | (static_cast<uint32>(g) << 8) |
            (static_cast<uint32>(r)));
}


uint32 FxColorFromFloats(float rgba[4])
{
    return ((static_cast<uint32>(rgba[3] * 255.0) << 24) | (static_cast<uint32>(rgba[2] * 255.0) << 16) |
            (static_cast<uint32>(rgba[1] * 255.0) << 8) | (static_cast<uint32>(rgba[0] * 255.0)));
}
