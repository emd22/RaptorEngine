#pragma once

#include <Math/Vec4.hpp>

#ifdef FX_NO_SIMD

namespace fx {

FX_FORCE_INLINE float32 Vec4f::LengthSquared() const { return ((X * X) + (Y * Y) + (Z * Z) + (W * W)); }

} // namespace fx

#endif
