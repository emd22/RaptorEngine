#pragma once

#include <Core/FxTypes.hpp>

#define FxDegToRad(deg) ((deg) * (M_PI / 180.0f))
#define FxRadToDeg(rad) ((rad) * (180.0f / M_PI))

namespace FxMath {

void SinCos(float32 rad, float32* out_sine, float32* out_cosine);

} // namespace FxMath
