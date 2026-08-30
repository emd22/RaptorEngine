/*
 * File:        SIMDHelper.hpp
 * Author:      emd22
 * Created:     25/08/2026
 * Description: Helper functions for SIMD (without vector classes). These are for basic operations that are similar
 * across platforms, to have cleaner vector code. Not to be confused with SSEUtil and NeonUtil, which are platform
 * specific vector helpers designed for math structures, and cover more advanced operations.
 */

#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#else
#include "SSE.hpp"
#endif

namespace fx {

#ifdef FX_USE_NEON


using UINT4 = uint32x4_t;
using FLOAT4 = float32x4_t;

namespace simd {

FX_FORCE_INLINE void StoreUInt4(unsigned int* dst, UINT4 v) { vst1q_u32(dst, v); }
FX_FORCE_INLINE void StoreFloat4(float* dst, FLOAT4 v) { vst1q_f32(dst, v); }

FX_FORCE_INLINE UINT4 LoadUInt4(const unsigned int* src) { return vld1q_u32(src); }
FX_FORCE_INLINE FLOAT4 LoadFloat4(const float* src) { return vld1q_f32(src); }
FX_FORCE_INLINE FLOAT4 LoadFloat4(float x, float y, float z, float w)
{
	const float sv alignas(16)[4] = { x, y, z, w };
	return vld1q_f32(sv);
}

FX_FORCE_INLINE FLOAT4 AbsDiff(FLOAT4 a, FLOAT4 b) { return vabdq_f32(a, b); }
FX_FORCE_INLINE FLOAT4 Sub(FLOAT4 a, FLOAT4 b) { return vsubq_f32(a, b); }

} // namespace simd

#else

using UINT4 = __m128i;
using FLOAT4 = __m128;

namespace simd {

FX_FORCE_INLINE void StoreUInt4(unsigned int* dst, UINT4 v) { _mm_storeu_si128(dst, v); }
FX_FORCE_INLINE void StoreFloat4(float* dst, FLOAT4 v) { _mm_storeu_ps(dst, v); }

FX_FORCE_INLINE UINT4 LoadUInt4(const unsigned int* src) { return _mm_loadu_si128(src); }
FX_FORCE_INLINE FLOAT4 LoadFloat4(const float* src) { return _mm_loadu_ps(src); }
FX_FORCE_INLINE FLOAT4 LoadFloat4(float x, float y, float z, float w)
{
	const float sv alignas(16)[4] = { x, y, z, w };
	return _mm_load_ps(sv);
}

FX_FORCE_INLINE FLOAT4 AbsDiff(FLOAT4 a, FLOAT4 b)
{
	FLOAT4 diff = _mm_sub_ps(a, b);
	// Remove the sign (abs)
	return _mm_and_ps(diff, _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF)));
}

FX_FORCE_INLINE FLOAT4 Sub(FLOAT4 a, FLOAT4 b) { return _mm_sub_ps(a, b); }

} // namespace simd

#endif

} // namespace fx
