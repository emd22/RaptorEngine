#pragma once

#include <Math/FxVec4.hpp>

#ifdef FX_USE_NEON

// Ignore

#else

FX_FORCE_INLINE float32 FxVec4f::LengthSquared() const { return ((X * X) + (Y * Y) + (Z * Z) + (W * W)); }

#endif
