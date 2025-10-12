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

} // namespace FxMath
