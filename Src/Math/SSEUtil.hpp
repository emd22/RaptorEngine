#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include "SSE.hpp"

#include <math.h>

#include <Core/Types.hpp>
#include <bit>

namespace fx::SSE {


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


constexpr uint32 scSignMask32 = 0x80000000;
static_assert(scSignMask32 == std::bit_cast<uint32>(-0.0f));

FX_FORCE_INLINE __m128 RemoveSign(__m128 vec)
{
	const __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32(scSignMask32));
	// Return the value with the sign bit removed
	return _mm_andnot_ps(sign_mask, vec);
}

template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE __m128 Permute4(__m128 a)
{
	// Assert that all components are for A
	static_assert(TComp1 < Shuffle_BX);
	static_assert(TComp2 < Shuffle_BX);
	static_assert(TComp3 < Shuffle_BX);
	static_assert(TComp4 < Shuffle_BX);

	constexpr uint8 permute = _MM_SHUFFLE(TComp4, TComp3, TComp2, TComp1);
	return _mm_permute_ps(a, permute);
}

template <eShuffleComponent TComp>
FX_FORCE_INLINE __m128 Permute4(__m128 a)
{
	static_assert(TComp < Shuffle_BX);

	constexpr uint8 permute = _MM_SHUFFLE(TComp, TComp, TComp, TComp);
	return _mm_permute_ps(a, permute);
}

/**
 * @brief Returns a single double vector rearranged to the template parameter values.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE __m256d Permute4d(__m256d a)
{
	// Assert that all components are for A
	static_assert(TComp1 < Shuffle_BX);
	static_assert(TComp2 < Shuffle_BX);
	static_assert(TComp3 < Shuffle_BX);
	static_assert(TComp4 < Shuffle_BX);

	constexpr uint8 permute = _MM_SHUFFLE(TComp4, TComp3, TComp2, TComp1);
	return _mm256_permute4x64_pd(a, permute);
}

/**
 * @brief Returns a double vector with the component `TComp` splatted across all lanes.
 */
template <eShuffleComponent TComp>
FX_FORCE_INLINE __m256d Permute4d(__m256d a)
{
	static_assert(TComp < Shuffle_BX);

	constexpr uint8 permute = _MM_SHUFFLE(TComp, TComp, TComp, TComp);
	return _mm256_permute4x64_pd(a, permute);
}

/**
 * @brief Returns a vector containing elements across both double vectors A and B, in the order of the template
 * parameters.
 */
template <eShuffleComponent TComp1, eShuffleComponent TComp2, eShuffleComponent TComp3, eShuffleComponent TComp4>
FX_FORCE_INLINE __m256d Permute4d(__m256d a, __m256d b)
{
	if constexpr (TComp1 < Shuffle_BX && TComp2 < Shuffle_BX && TComp3 < Shuffle_BX && TComp4 < Shuffle_BX) {
		return Permute4d<TComp1, TComp2, TComp3, TComp4>(a);
	}
	else if constexpr (TComp1 >= Shuffle_BX && TComp2 >= Shuffle_BX && TComp3 >= Shuffle_BX && TComp4 >= Shuffle_BX) {
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

		const __m256d even = _mm256_permute2f128_pd(a, b, even_halves);
		const __m256d odd = _mm256_permute2f128_pd(a, b, odd_halves);

		// Pick the lane inside of each half, alternating between even and odd
		constexpr int lanes = (TComp1 & 0x01) | ((TComp2 & 0x01) << 1) | ((TComp3 & 0x01) << 2) |
							  ((TComp4 & 0x01) << 3);

		return _mm256_shuffle_pd(even, odd, lanes);
	}
}


FX_FORCE_INLINE float32 Dot(__m128 a, __m128 b) { return _mm_cvtss_f32(_mm_dp_ps(a, b, 0xFF)); }

FX_FORCE_INLINE __m128 Cross(__m128 a, __m128 b)
{
	const __m128 a_yzxw = SSE::Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(a);
	const __m128 b_yzxw = SSE::Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(b);

	const __m128 result_yzxw = _mm_sub_ps(_mm_mul_ps(a, b_yzxw), _mm_mul_ps(a_yzxw, b));

	return SSE::Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(result_yzxw);
}

FX_FORCE_INLINE __m128 Negate(__m128 a) { return _mm_xor_ps(a, _mm_castsi128_ps(_mm_set1_epi32(scSignMask32))); }

/**
 * @brief Sets the signs of all components of `v` to the sign of `TSign`
 * @tparam TSign A value (e.g. 1.0 or -1.0) to specify the sign.
 */
template <int TSign>
FX_FORCE_INLINE __m128 SetSigns(__m128 v)
{
	const __m128 sign_v = _mm_castsi128_ps(_mm_set1_epi32(scSignMask32));

	if constexpr (TSign > 0.0) {
		// Return the value without the sign bit (always positive)
		return _mm_andnot_ps(sign_v, v);
	}

	// Return the value with the sign bit (always negative)
	return _mm_or_ps(v, sign_v);
}

template <int TX, int TY, int TZ, int TW>
FX_FORCE_INLINE __m128 FlipSigns(__m128 v)
{
	constexpr float32 signs alignas(16)[4] = {
		TX > 0 ? 0.0f : -0.0f,
		TY > 0 ? 0.0f : -0.0f,
		TZ > 0 ? 0.0f : -0.0f,
		TW > 0 ? 0.0f : -0.0f,
	};

	const __m128 sign_v = _mm_load_ps(signs);
	return _mm_xor_ps(v, sign_v);
}


FX_FORCE_INLINE float32 LengthSquared(__m128 vec) { return _mm_cvtss_f32(_mm_dp_ps(vec, vec, 0xF1)); }

FX_FORCE_INLINE float32 Length(__m128 vec) { return sqrtf(LengthSquared(vec)); }

FX_FORCE_INLINE __m128 Normalize(__m128 vec)
{
	// Calculate length and splat to register
	const __m128 len_v = _mm_set1_ps(Length(vec));

	// Divide vector by length and return
	return _mm_div_ps(vec, len_v);
}

void SinCos4(__m128 x, __m128* ysin, __m128* ycos);

}; // namespace fx::SSE

#endif
