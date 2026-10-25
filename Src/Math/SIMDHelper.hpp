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

namespace simd {

enum eShuffleComponent
{
	/* A Vector */
	Shuffle_AX = 0,
	Shuffle_AY = 1,
	Shuffle_AZ = 2,
	Shuffle_AW = 3,

	/* B Vector */
	Shuffle_BX = 4,
	Shuffle_BY = 5,
	Shuffle_BZ = 6,
	Shuffle_BW = 7,
};

template <eShuffleComponent TA, eShuffleComponent TB, eShuffleComponent TC, eShuffleComponent TD>
concept C_SinglePermute = (TA <= Shuffle_AW) && (TB <= Shuffle_AW) && (TC <= Shuffle_AW) && (TD <= Shuffle_AW);

template <eShuffleComponent TA, eShuffleComponent TB, eShuffleComponent TC, eShuffleComponent TD>
concept C_SinglePermuteForB = (TA >= Shuffle_BX) && (TB >= Shuffle_BX) && (TC >= Shuffle_BX) && (TD >= Shuffle_BX);

template <eShuffleComponent TA, eShuffleComponent TB, eShuffleComponent TC, eShuffleComponent TD>
concept C_AllComponentsSame = (TA == TB) && (TB == TC) && (TC == TD);

} // namespace simd

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

FX_FORCE_INLINE FLOAT4 Round(FLOAT4 value) { return vrndnq_f32(value); }
FX_FORCE_INLINE FLOAT4 Floor(FLOAT4 value) { return vrndmq_f32(value); }

FX_FORCE_INLINE FLOAT4 Min(FLOAT4 a, FLOAT4 b) { return vminq_f32(a, b); }
FX_FORCE_INLINE FLOAT4 Max(FLOAT4 a, FLOAT4 b) { return vmaxq_f32(a, b); }

FX_FORCE_INLINE FLOAT4 Select(UINT4 mask, FLOAT4 a, FLOAT4 b) { return vbslq_f32(mask, a, b); }

FX_FORCE_INLINE FLOAT4 Add(FLOAT4 a, FLOAT4 b) { return vaddq_f32(a, b); }
FX_FORCE_INLINE FLOAT4 Mul(FLOAT4 a, FLOAT4 b) { return vmulq_f32(a, b); }
FX_FORCE_INLINE FLOAT4 Div(FLOAT4 a, FLOAT4 b) { return vdivq_f32(a, b); }
FX_FORCE_INLINE FLOAT4 Abs(FLOAT4 value) { return vabsq_f32(value); }

FX_FORCE_INLINE UINT4 CompareLess(FLOAT4 a, FLOAT4 b) { return vcltq_f32(a, b); }
FX_FORCE_INLINE UINT4 CompareLessEqual(FLOAT4 a, FLOAT4 b) { return vcleq_f32(a, b); }
FX_FORCE_INLINE UINT4 And(UINT4 a, UINT4 b) { return vandq_u32(a, b); }

FX_FORCE_INLINE uint32 GetMask(UINT4 mask)
{
	const uint32 weights alignas(16)[4] = { 1, 2, 4, 8 };
	return vaddvq_u32(vandq_u32(mask, vld1q_u32(weights)));
}

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

////////////////////////////////
// Permute/Reorder Functions
////////////////////////////////

/**
 * @brief Returns a single vector rearranged to the template parameter values.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a)
	requires C_SinglePermute<TComp1, TComp2, TComp3, TComp4>
{
	// Unspecialized shuffle, reorder the lanes directly

	FLOAT4 result;
	result = vmovq_n_f32(vgetq_lane_f32(a, TComp1 & 0x03));
	result = vsetq_lane_f32(vgetq_lane_f32(a, TComp2 & 0x03), result, 1);
	result = vsetq_lane_f32(vgetq_lane_f32(a, TComp3 & 0x03), result, 2);
	result = vsetq_lane_f32(vgetq_lane_f32(a, TComp4 & 0x03), result, 3);

	return result;
}

// Specializations


template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AW, Shuffle_AZ, Shuffle_AY, Shuffle_AX>(FLOAT4 a)
{
	return vcombine_f32(vrev64_f32(vget_high_f32(a)), vrev64_f32(vget_low_f32(a)));
}

template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AX, Shuffle_AY, Shuffle_AZ, Shuffle_AW>(FLOAT4 a)
{
	return a;
}

template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AX, Shuffle_AY, Shuffle_AZ, Shuffle_AZ>(FLOAT4 a)
{
	return vcombine_f32(vget_low_f32(a), vdup_lane_f32(vget_high_f32(a), 0));
}

template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AX, Shuffle_AY, Shuffle_AW, Shuffle_AW>(FLOAT4 a)
{
	return vcombine_f32(vget_low_f32(a), vdup_lane_f32(vget_high_f32(a), 1));
}


template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AY, Shuffle_AX, Shuffle_AW, Shuffle_AZ>(FLOAT4 a)
{
	return vcombine_f32(vrev64_f32(vget_low_f32(a)), vrev64_f32(vget_high_f32(a)));
}

template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AZ, Shuffle_AZ, Shuffle_AY, Shuffle_AX>(FLOAT4 a)
{
	return vcombine_f32(vdup_lane_f32(vget_high_f32(a), 0), vrev64_f32(vget_low_f32(a)));
}

template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AZ, Shuffle_AW, Shuffle_AX, Shuffle_AY>(FLOAT4 a)
{
	return vcombine_f32(vget_high_f32(a), vget_low_f32(a));
}

template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AX>(FLOAT4 a)
{
	static uint8x16_t table = { 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
								0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03 };
	return vreinterpretq_f32_u8(vqtbl1q_u8(vreinterpretq_u8_f32(a), table));
}

/**
 * @brief Returns the vector A rearranged to the components {Y, Z, X, W}.
 */
template <>
FX_FORCE_INLINE FLOAT4 Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(FLOAT4 a)
{
	// Extend vector (XYZW -> YZWX)
	a = vextq_f32(a, a, 1);

	// Get the low components (YZ)
	const float32x2_t a_lo = vget_low_f32(a);

	// Reverse high componets of YZWX (WX -> XW)
	const float32x2_t a_hi = vrev64_f32(vget_high_f32(a));

	// Return result YZXW
	return vcombine_f32(a_lo, a_hi);
}

template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
	requires C_SinglePermute<TComp1, TComp2, TComp3, TComp4> && C_AllComponentsSame<TComp1, TComp2, TComp3, TComp4>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a)
{
	return vdupq_laneq_f32(a, TComp1);
}

/**
 * @brief Returns a vector with the component `TComp` splatted across all lanes.
 */
template <eShuffleComponent TComp>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a)
{
	static_assert(TComp < Shuffle_BX);

	return vdupq_laneq_f32(a, TComp);
}

/**
 * @brief Returns a vector containing elements across both vectors A and B, in the order of the template parameters.
 *
 * For example, given a vector A = {1, 2, 3, 4} and another vector B = {5, 6, 7, 8},
 * ```
 * // Returns {1, 6, 3, 8}
 * Permute4<Shuffle_AX, Shuffle_BY, Shuffle_AZ, Shuffle_BW>(A, B);
 * ```
 *
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a, FLOAT4 b)
{
	// Unspecialized shuffle, reorder the lanes directly

	FLOAT4 result;
	result = vmovq_n_f32(vgetq_lane_f32(TComp1 >= Shuffle_BX ? b : a, TComp1 & 0x03));
	result = vsetq_lane_f32(vgetq_lane_f32(TComp2 >= Shuffle_BX ? b : a, TComp2 & 0x03), result, 1);
	result = vsetq_lane_f32(vgetq_lane_f32(TComp3 >= Shuffle_BX ? b : a, TComp3 & 0x03), result, 2);
	result = vsetq_lane_f32(vgetq_lane_f32(TComp4 >= Shuffle_BX ? b : a, TComp4 & 0x03), result, 3);

	return result;
}

/**
 * @brief Returns the single permutation of the two vectors A and B, using the components passed in via template
 * parameters.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a, FLOAT4 b)
	requires C_SinglePermute<TComp1, TComp2, TComp3, TComp4>
{
	return Permute4<TComp1, TComp2, TComp3, TComp4>(a);
}

/**
 * @brief Returns the vector B shuffled to the order passed in from the template arguments.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a, FLOAT4 b)
	requires C_SinglePermuteForB<TComp1, TComp2, TComp3, TComp4>
{
	// Adjust indices for single permutations
	return Permute4<
		static_cast<eShuffleComponent>(TComp1 - Shuffle_BX), static_cast<eShuffleComponent>(TComp2 - Shuffle_BX),
		static_cast<eShuffleComponent>(TComp3 - Shuffle_BX), static_cast<eShuffleComponent>(TComp4 - Shuffle_BX)>(b);
}

////////////////////////////////
// Double Permute Functions
////////////////////////////////

namespace detail {

/**
 * @brief Returns the DOUBLE2 that holds the component `TComp`, from either vector A or B.
 */
template <eShuffleComponent TComp>
FX_FORCE_INLINE DOUBLE2 GetDoubleHalf(const DOUBLE4& a, const DOUBLE4& b)
{
	if constexpr (TComp >= Shuffle_BX) {
		return b.val[(TComp >> 1) & 0x01];
	}
	return a.val[TComp >> 1];
}

/**
 * @brief Returns {a[TLaneA], b[TLaneB]}. Every combination maps to a single instruction.
 */
template <uint32 TLaneA, uint32 TLaneB>
FX_FORCE_INLINE DOUBLE2 Permute2d(DOUBLE2 a, DOUBLE2 b)
{
	if constexpr (TLaneA == 0 && TLaneB == 0) {
		return vzip1q_f64(a, b);
	}
	else if constexpr (TLaneA == 1 && TLaneB == 1) {
		return vzip2q_f64(a, b);
	}
	else if constexpr (TLaneA == 1 && TLaneB == 0) {
		return vextq_f64(a, b, 1);
	}
	else {
		// {a.x, b.y}, this is a no-op if a and b are the same register
		return vcopyq_laneq_f64(a, 1, b, 1);
	}
}

} // namespace detail

/**
 * @brief Returns a vector containing elements across both double vectors A and B, in the order of the template
 * parameters. Same as the FLOAT4 version of Permute4.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE DOUBLE4 Permute4d(DOUBLE4 a, DOUBLE4 b)
{
	DOUBLE4 result;

	result.val[0] = detail::Permute2d<TComp1 & 0x01, TComp2 & 0x01>(detail::GetDoubleHalf<TComp1>(a, b),
																	detail::GetDoubleHalf<TComp2>(a, b));
	result.val[1] = detail::Permute2d<TComp3 & 0x01, TComp4 & 0x01>(detail::GetDoubleHalf<TComp3>(a, b),
																	detail::GetDoubleHalf<TComp4>(a, b));

	return result;
}

/**
 * @brief Returns a single double vector rearranged to the template parameter values.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE DOUBLE4 Permute4d(DOUBLE4 a)
{
	static_assert(C_SinglePermute<TComp1, TComp2, TComp3, TComp4>);

	return Permute4d<TComp1, TComp2, TComp3, TComp4>(a, a);
}

/**
 * @brief Returns a double vector with the component `TComp` splatted across all lanes.
 */
template <eShuffleComponent TComp>
FX_FORCE_INLINE DOUBLE4 Permute4d(DOUBLE4 a)
{
	static_assert(TComp < Shuffle_BX);

	const DOUBLE2 splat = vdupq_laneq_f64(a.val[TComp >> 1], TComp & 0x01);
	return DOUBLE4 { { splat, splat } };
}


} // namespace simd

#else

using UINT4 = __m128i;
using FLOAT4 = __m128;

using DOUBLE2 = __m128d;
using DOUBLE4 = __m256d;

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

FX_FORCE_INLINE FLOAT4 Add(FLOAT4 a, FLOAT4 b) { return _mm_add_ps(a, b); }
FX_FORCE_INLINE FLOAT4 Mul(FLOAT4 a, FLOAT4 b) { return _mm_mul_ps(a, b); }
FX_FORCE_INLINE FLOAT4 Div(FLOAT4 a, FLOAT4 b) { return _mm_div_ps(a, b); }
FX_FORCE_INLINE FLOAT4 Abs(FLOAT4 value) { return _mm_and_ps(value, _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF))); }

FX_FORCE_INLINE UINT4 CompareLess(FLOAT4 a, FLOAT4 b) { return _mm_castps_si128(_mm_cmplt_ps(a, b)); }
FX_FORCE_INLINE UINT4 CompareLessEqual(FLOAT4 a, FLOAT4 b) { return _mm_castps_si128(_mm_cmple_ps(a, b)); }
FX_FORCE_INLINE UINT4 And(UINT4 a, UINT4 b) { return _mm_and_si128(a, b); }

FX_FORCE_INLINE uint32 GetMask(UINT4 mask) { return static_cast<uint32>(_mm_movemask_ps(_mm_castsi128_ps(mask))); }

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

/////////////////////////////////////
// Double Precision
/////////////////////////////////////

/**
 * @brief Load a block of four doubles directly into the vector (unaligned).
 *
 * @note Make sure this buffer is valid or it WILL cause a GP fault.
 */
FX_FORCE_INLINE DOUBLE4 LoadDouble4(const double* src) { return _mm256_loadu_pd(src); }
FX_FORCE_INLINE DOUBLE4 LoadDouble4(double x, double y, double z, double w) { return _mm256_setr_pd(x, y, z, w); }
FX_FORCE_INLINE DOUBLE4 LoadDouble4(const double scalar) { return _mm256_set1_pd(scalar); }

/**
 * @brief Get the sum of all four lanes of the product of two DOUBLE4's (a full 4-component dot product).
 */
FX_FORCE_INLINE double Dot(DOUBLE4 a, DOUBLE4 b)
{
	const __m256d prod = _mm256_mul_pd(a, b);

	// Add the upper half onto the lower half, then add the two remaining lanes
	__m128d sum = _mm_add_pd(_mm256_castpd256_pd128(prod), _mm256_extractf128_pd(prod, 1));
	sum = _mm_add_sd(sum, _mm_unpackhi_pd(sum, sum));

	return _mm_cvtsd_f64(sum);
}

////////////////////////////////
// Permute/Reorder Functions
////////////////////////////////

/**
 * @brief Returns a single vector rearranged to the template parameter values.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a)
{
	// Assert that all components are for A
	static_assert(C_SinglePermute<TComp1, TComp2, TComp3, TComp4>);

	constexpr uint8 permute = _MM_SHUFFLE(TComp4, TComp3, TComp2, TComp1);
	return _mm_permute_ps(a, permute);
}

/**
 * @brief Returns a vector with the component `TComp` splatted across all lanes.
 */
template <eShuffleComponent TComp>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a)
{
	static_assert(TComp < Shuffle_BX);

	constexpr uint8 permute = _MM_SHUFFLE(TComp, TComp, TComp, TComp);
	return _mm_permute_ps(a, permute);
}

/**
 * @brief Returns a vector containing elements across both vectors A and B, in the order of the template parameters.
 *
 * For example, given a vector A = {1, 2, 3, 4} and another vector B = {5, 6, 7, 8},
 * ```
 * // Returns {1, 6, 3, 8}
 * Permute4<Shuffle_AX, Shuffle_BY, Shuffle_AZ, Shuffle_BW>(A, B);
 * ```
 *
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE FLOAT4 Permute4(FLOAT4 a, FLOAT4 b)
{
	if constexpr (C_SinglePermute<TComp1, TComp2, TComp3, TComp4>) {
		return Permute4<TComp1, TComp2, TComp3, TComp4>(a);
	}
	else if constexpr (C_SinglePermuteForB<TComp1, TComp2, TComp3, TComp4>) {
		return Permute4<
			static_cast<eShuffleComponent>(TComp1 - Shuffle_BX), static_cast<eShuffleComponent>(TComp2 - Shuffle_BX),
			static_cast<eShuffleComponent>(TComp3 - Shuffle_BX), static_cast<eShuffleComponent>(TComp4 - Shuffle_BX)>(b);
	}
	else if constexpr (TComp1 < Shuffle_BX && TComp2 < Shuffle_BX && TComp3 >= Shuffle_BX && TComp4 >= Shuffle_BX) {
		constexpr uint8 shuffle = _MM_SHUFFLE(TComp4 & 0x03, TComp3 & 0x03, TComp2, TComp1);
		return _mm_shuffle_ps(a, b, shuffle);
	}
	else {
		constexpr uint8 permute = _MM_SHUFFLE(TComp4 & 0x03, TComp3 & 0x03, TComp2 & 0x03, TComp1 & 0x03);
		constexpr int from_b = (TComp1 >> 2) | ((TComp2 >> 2) << 1) | ((TComp3 >> 2) << 2) | ((TComp4 >> 2) << 3);

		return _mm_blend_ps(_mm_permute_ps(a, permute), _mm_permute_ps(b, permute), from_b);
	}
}

/**
 * @brief Returns a single double vector rearranged to the template parameter values.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE DOUBLE4 Permute4d(DOUBLE4 a)
{
	// Assert that all components are for A
	static_assert(C_SinglePermute<TComp1, TComp2, TComp3, TComp4>);

	constexpr uint8 permute = _MM_SHUFFLE(TComp4, TComp3, TComp2, TComp1);
	return _mm256_permute4x64_pd(a, permute);
}

/**
 * @brief Returns a double vector with the component `TComp` splatted across all lanes.
 */
template <eShuffleComponent TComp>
FX_FORCE_INLINE DOUBLE4 Permute4d(DOUBLE4 a)
{
	static_assert(TComp < Shuffle_BX);

	constexpr uint8 permute = _MM_SHUFFLE(TComp, TComp, TComp, TComp);
	return _mm256_permute4x64_pd(a, permute);
}

/**
 * @brief Returns a vector containing elements across both double vectors A and B, in the order of the template
 * parameters. Same as the FLOAT4 version of Permute4.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE DOUBLE4 Permute4d(DOUBLE4 a, DOUBLE4 b)
{
	if constexpr (C_SinglePermute<TComp1, TComp2, TComp3, TComp4>) {
		return Permute4d<TComp1, TComp2, TComp3, TComp4>(a);
	}
	else if constexpr (C_SinglePermuteForB<TComp1, TComp2, TComp3, TComp4>) {
		return Permute4d<
			static_cast<eShuffleComponent>(TComp1 - Shuffle_BX), static_cast<eShuffleComponent>(TComp2 - Shuffle_BX),
			static_cast<eShuffleComponent>(TComp3 - Shuffle_BX), static_cast<eShuffleComponent>(TComp4 - Shuffle_BX)>(
			b);
	}
	else {
		// Index >> 1 selects the 128-bit half to use: 0 = A.lo, 1 = A.hi, 2 = B.lo, 3 = B.hi.
		// Gather the halves that hold the even output components, and the halves that hold the odd output components.
		constexpr int even_halves = (TComp1 >> 1) | ((TComp3 >> 1) << 4);
		constexpr int odd_halves = (TComp2 >> 1) | ((TComp4 >> 1) << 4);

		const DOUBLE4 even = _mm256_permute2f128_pd(a, b, even_halves);
		const DOUBLE4 odd = _mm256_permute2f128_pd(a, b, odd_halves);

		// Pick the lane inside of each half, alternating between even and odd
		constexpr int lanes = (TComp1 & 0x01) | ((TComp2 & 0x01) << 1) | ((TComp3 & 0x01) << 2) |
							  ((TComp4 & 0x01) << 3);

		return _mm256_shuffle_pd(even, odd, lanes);
	}
}


} // namespace simd

#endif

} // namespace fx
