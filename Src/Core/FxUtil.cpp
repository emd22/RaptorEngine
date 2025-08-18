#include "FxUtil.hpp"

uint32 FxColorFromRGBA(uint8 r, uint8 g, uint8 b, uint8 a)
{
    return (
        (static_cast<uint32>(r) << 24) |
        (static_cast<uint32>(g) << 16) |
        (static_cast<uint32>(b) << 8)  |
        (static_cast<uint32>(a))
    );
}


uint32 FxColorFromFloats(float rgba[4])
{
    return (
        (static_cast<uint32>(rgba[0]) << 24) |
        (static_cast<uint32>(rgba[1]) << 16) |
        (static_cast<uint32>(rgba[2]) << 8)  |
        (static_cast<uint32>(rgba[3]))
    );
}
