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
#include <Core/Types.hpp>

#ifdef FX_USE_NEON
#include <arm_neon.h>
#else
#include "SSE.hpp"
#endif

#include <bit>

namespace fx {

#ifdef FX_USE_NEON


using UINT4 = uint32x4_t;
using FLOAT4 = float32x4_t;

using DOUBLE2 = float64x2_t;

/// A combined set of two double vectors. This is the closest we can get to __m256 with NEON.
using DOUBLE4 = float64x2x2_t;

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

FX_FORCE_INLINE FLOAT4 LoadFloat4(float scalar) { return vdupq_n_f32(scalar); }

FX_FORCE_INLINE FLOAT4 AbsDiff(FLOAT4 a, FLOAT4 b) { return vabdq_f32(a, b); }
FX_FORCE_INLINE FLOAT4 Sub(FLOAT4 a, FLOAT4 b) { return vsubq_f32(a, b); }

FX_FORCE_INLINE FLOAT4 Round(FLOAT4 value) { return vrndq_f32(value); }
FX_FORCE_INLINE FLOAT4 Floor(FLOAT4 value) { return vrndmq_f32(value); }

FX_FORCE_INLINE FLOAT4 Min(FLOAT4 a, FLOAT4 b) { return vminq_f32(a, b); }
FX_FORCE_INLINE FLOAT4 Max(FLOAT4 a, FLOAT4 b) { return vmaxq_f32(a, b); }

FX_FORCE_INLINE FLOAT4 Select(UINT4 mask, FLOAT4 a, FLOAT4 b) { return vbslq_f32(mask, a, b); }

/**
 * @brief Returns the signs of a vector. 1.0 if the component is positive or -1.0 if negative.
 * Follows the same logic as the version I wrote in MathUtil
 */
FX_FORCE_INLINE FLOAT4 GetSign(FLOAT4 v)
{
	const uint32x4_t v_sign_mask = vdupq_n_u32(0x80000000U);
	const uint32x4_t v_one = vdupq_n_u32(std::bit_cast<unsigned int>(1.0f));

	// The bare sign bit
	const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(v), v_sign_mask);

	// Add back in the `1.0`. We could return negative or positive zero, but that is not very useful in
	// multiplications...
	return vreinterpretq_u32_f32(vorrq_u32(sign, v_one));
}

/////////////////////////////////////
// Double Precision
/////////////////////////////////////

/**
 * @brief Load a block of doubles directly into the vector.
 *
 * @note Make sure this buffer is valid or it WILL cause a GP fault.
 *
 * @note Other platforms use this as an unaligned load, but NEON automatically deals with unaligned boundaries for load
 * instructions. Keep in mind that there could be a small but noticable performance or latency penalty to this when
 * running under AVX. If you can, use the scalar version of this function to ensure an aligned load.
 */
FX_FORCE_INLINE DOUBLE4 LoadDouble4(const double* src) { return vld1q_f64_x2(src); }
FX_FORCE_INLINE DOUBLE4 LoadDouble4(double x, double y, double z, double w)
{
	const double sv alignas(16)[4] = { x, y, z, w };
	return vld1q_f64_x2(sv);
}

FX_FORCE_INLINE DOUBLE4 LoadDouble4(const double scalar)
{
	return DOUBLE4 { { vdupq_n_f64(scalar), vdupq_n_f64(scalar) } };
}

/**
 * @brief Get the dot product of two DOUBLE2's, with the result being returned in the first lane of a DOUBLE2.
 */
FX_FORCE_INLINE DOUBLE2 DotV(DOUBLE2 a, DOUBLE2 b)
{
	// Get the product of the two vectors
	DOUBLE2 prod = vmulq_f64(a, b);
	// No REV here, so we need to extract to flip the vector
	DOUBLE2 rev_v = vextq_f64(prod, prod, 1);
	return vaddq_f64(prod, rev_v);
}

FX_FORCE_INLINE double Dot(DOUBLE2 a, DOUBLE2 b)
{
	DOUBLE2 prod = vmulq_f64(a, b);
	return vaddvq_f64(prod);
}

FX_FORCE_INLINE double Dot(DOUBLE4 a, DOUBLE4 b)
{
	DOUBLE2 result = vaddq_f64(DotV(a.val[0], b.val[0]), DotV(a.val[1], b.val[1]));

	// Return the first lane (result of both of em)
	return vgetq_lane_f64(result, 0);
}


} // namespace simd

#else

using UINT4 = __m128i;
using FLOAT4 = __m128;

namespace simd {

FX_FORCE_INLINE void StoreUInt4(unsigned int* dst, UINT4 v) { _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), v); }
FX_FORCE_INLINE void StoreFloat4(float* dst, FLOAT4 v) { _mm_storeu_ps(dst, v); }

FX_FORCE_INLINE UINT4 LoadUInt4(const unsigned int* src)
{
	return _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
}
FX_FORCE_INLINE FLOAT4 LoadFloat4(const float* src) { return _mm_loadu_ps(src); }
FX_FORCE_INLINE FLOAT4 LoadFloat4(float x, float y, float z, float w)
{
	const float sv alignas(16)[4] = { x, y, z, w };
	return _mm_load_ps(sv);
}

FX_FORCE_INLINE FLOAT4 LoadFloat4(float scalar) { return _mm_set1_ps(scalar); }

FX_FORCE_INLINE FLOAT4 AbsDiff(FLOAT4 a, FLOAT4 b)
{
	FLOAT4 diff = _mm_sub_ps(a, b);
	// Remove the sign (abs)
	return _mm_and_ps(diff, _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF)));
}

FX_FORCE_INLINE FLOAT4 Sub(FLOAT4 a, FLOAT4 b) { return _mm_sub_ps(a, b); }

FX_FORCE_INLINE FLOAT4 Round(FLOAT4 value) { return _mm_round_ps(value, _MM_FROUND_TO_NEAREST_INT); }
FX_FORCE_INLINE FLOAT4 Floor(__m128 value) { return _mm_floor_ps(value); }

FX_FORCE_INLINE FLOAT4 Min(FLOAT4 a, FLOAT4 b) { return _mm_min_ps(a, b); }
FX_FORCE_INLINE FLOAT4 Max(FLOAT4 a, FLOAT4 b) { return _mm_max_ps(a, b); }

FX_FORCE_INLINE FLOAT4 Select(UINT4 mask, FLOAT4 a, FLOAT4 b) { return _mm_blendv_ps(b, a, _mm_castsi128_ps(mask)); }

/**
 * @brief Returns the signs of a vector. 1.0 if the component is positive or -1.0 if negative.
 * Follows the same logic as the version I wrote in MathUtil, and the same as the Neon implementation above
 */
FX_FORCE_INLINE FLOAT4 GetSign(FLOAT4 v)
{
	constexpr uint32 sign_mask = 0x80000000U;
	constexpr uint32 one = std::bit_cast<unsigned int>(1.0f);

	const __m128i v_sign_mask = _mm_set1_epi32(std::bit_cast<int>(sign_mask));
	const __m128i v_one = _mm_set1_epi32(std::bit_cast<int>(one));

	// The bare sign bit
	const __m128 sign = _mm_and_ps(v, _mm_castsi128_ps(v_sign_mask));

	// Add back in the `1.0`. We could return negative or positive zero, but that is not very useful in
	// multiplications...
	return _mm_or_ps(sign, _mm_castsi128_ps(v_one));
}


} // namespace simd

#endif

} // namespace fx
