#pragma once

#include <Core/Defines.hpp>

namespace fx {

#ifdef FX_USE_NEON

#include <arm_neon.h>

using UINT4 = uint32x4_t;
using FLOAT4 = float32x4_t;

#else

#include <immintrin.h>

using UINT4 = __m128i;
using FLOAT4 = __m128;

#endif

} // namespace fx
