#pragma once

#include <Core/Types.hpp>

namespace fx {

struct Color
{
public:
    // Colour definitions
    static const Color sNone;
    static const Color sTransparent;

    static const Color sWhite;
    static const Color sBlack;
    static const Color sRed;
    static const Color sBlue;
    static const Color sGreen;

public:
    Color() {}
    Color(uint32 value) : Value(value) {}
    Color(uint32 value, uint8 brightness)
    {
        Value = value;
        A = brightness;
    }

    static FX_FORCE_INLINE Color FromRGBA(uint8 r, uint8 g, uint8 b, uint8 a);
    static FX_FORCE_INLINE Color FromFloats(float32 rgba[4]);

    FX_FORCE_INLINE uint32 AsUInt() const { return Value; }

public:
    union
    {
        struct
        {
            uint8 R, G, B, A;
        };

        uint32 Value;
    };
};

///////////////////////////
// Definitions
///////////////////////////

FX_FORCE_INLINE Color Color::FromRGBA(uint8 r, uint8 g, uint8 b, uint8 a)
{
    Color colour;
    colour.Value = ((static_cast<uint32>(a) << 24) | (static_cast<uint32>(b) << 16) | (static_cast<uint32>(g) << 8) |
                    (static_cast<uint32>(r)));
    return colour;
}

FX_FORCE_INLINE Color Color::FromFloats(float32 rgba[4])
{
    Color colour;
    colour.Value = ((static_cast<uint32>(rgba[3] * 255.0) << 24) | (static_cast<uint32>(rgba[2] * 255.0) << 16) |
                    (static_cast<uint32>(rgba[1] * 255.0) << 8) | (static_cast<uint32>(rgba[0] * 255.0)));
    return colour;
}

enum class eColorComponent
{
    R = (1 << 0),
    G = (1 << 1),
    B = (1 << 2),
    A = (1 << 3),

    RG = R | G,
    RGB = RG | B,
    RGBA = RGB | A,
};

FxEnumFlags(eColorComponent);

} // namespace fx
