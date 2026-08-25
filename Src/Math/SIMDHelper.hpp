#pragma once

#include <Core/Defines.hpp>

namespace fx {

#ifdef FX_USE_NEON

#include <arm_neon.h>

using UINT4 = uint32x4_t;
using FLOAT4 = float32x4_t;

namespace simd {

FX_FORCE_INLINE void StoreUInt4(unsigned int* dst, UINT4 v) { vst1q_u32(dst, v); }
FX_FORCE_INLINE void StoreFloat4(float* dst, FLOAT4 v) { vst1q_f32(dst, v); }

FX_FORCE_INLINE UINT4 LoadUInt4(const unsigned int* src) { return vld1q_u32(src); }
FX_FORCE_INLINE FLOAT4 LoadFloat4(const float* src) { return vld1q_f32(src); }

} // namespace simd

#else

#include <immintrin.h>

using UINT4 = __m128i;
using FLOAT4 = __m128;

namespace simd {

FX_FORCE_INLINE void StoreUInt4(unsigned int* dst, UINT4 v) { _mm_storeu_si128(dst, v); }
FX_FORCE_INLINE void StoreFloat4(float* dst, FLOAT4 v) { _mm_storeu_ps(dst, v); }

FX_FORCE_INLINE UINT4 LoadUInt4(const unsigned int* src) { return _mm_loadu_si128(src); }
FX_FORCE_INLINE FLOAT4 LoadFloat4(const float* src) { return _mm_loadu_ps(src); }

} // namespace simd

#endif

} // namespace fx
