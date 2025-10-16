#pragma once

#include <Core/FxTypes.hpp>

#define FxDegToRad(deg) ((deg) * (M_PI / 180.0f))
#define FxRadToDeg(rad) ((rad) * (180.0f / M_PI))

#include <cmath>

namespace FxMath {

void SinCos(float32 rad, float32* out_sine, float32* out_cosine);

static FX_FORCE_INLINE float32 Clamp(float32 value, float32 lower, float32 upper)
{
    return fmin(fmax(value, lower), upper);
}

template <uint16 TAlignTo>
FX_FORCE_INLINE uint64 AlignValue(uint64 value)
{
    // Generic case

    const uint16 remainder = (value % TAlignTo);

    // If the value is not aligned(there is a remainder in the division), offset our value by the missing bytes
    if (remainder != 0) {
        value += (TAlignTo - remainder);
    }

    return value;
}

template <>
FX_FORCE_INLINE uint64 AlignValue<2>(uint64 value)
{
    // Get the bottom four bits (value % 16)
    const uint8 remainder = (value & 0x01);

    if (remainder != 0) {
        ++value;
    }

    return value;
}


template <>
FX_FORCE_INLINE uint64 AlignValue<4>(uint64 value)
{
    // Get the bottom four bits (value % 16)
    const uint8 remainder = (value & 0x03);

    if (remainder != 0) {
        value += (4 - remainder);
    }

    return value;
}


template <>
FX_FORCE_INLINE uint64 AlignValue<8>(uint64 value)
{
    // Get the bottom four bits (value % 16)
    const uint8 remainder = (value & 0x07);

    if (remainder != 0) {
        value += (8 - remainder);
    }

    return value;
}

template <>
FX_FORCE_INLINE uint64 AlignValue<16>(uint64 value)
{
    // Get the bottom four bits (value % 16)
    const uint8 remainder = (value & 0x0F);

    if (remainder != 0) {
        value += (16 - remainder);
    }

    return value;
}

template <>
FX_FORCE_INLINE uint64 AlignValue<32>(uint64 value)
{
    // Get the bottom four bits (value % 32)
    const uint8 remainder = (value & 0x1F);

    if (remainder != 0) {
        value += (32 - remainder);
    }

    return value;
}

} // namespace FxMath
