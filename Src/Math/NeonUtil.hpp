#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_NEON

#include <arm_neon.h>
#include <bit>
#include <math.h>

#include <Core/Types.hpp>

#include "SIMDHelper.hpp"

namespace fx::Neon {

constexpr uint32 scSignMask32 = 0x80000000;
static_assert(scSignMask32 == std::bit_cast<uint32>(-0.0f));

FX_FORCE_INLINE float32 LengthSquared(float32x4_t vec)
{
	// Square the vector
	vec = vmulq_f32(vec, vec);

	// Add all components (horizontal add)
	const float32 len2 = vaddvq_f32(vec);

	return len2;
}

FX_FORCE_INLINE float32 Length(float32x4_t vec) { return sqrtf(LengthSquared(vec)); }

FX_FORCE_INLINE float32x4_t Normalize(float32x4_t vec)
{
	// Load vector into register
	const float32x4_t res = vec;

	// Calculate length and splat to register
	const float32x4_t len_v = vdupq_n_f32(Length(vec));

	// Divide vector by length and return
	return vdivq_f32(res, len_v);
}

FX_FORCE_INLINE uint32x4_t ToUInt(float32x4_t vec) { return vcvtq_u32_f32(vec); }
FX_FORCE_INLINE int32x4_t ToInt(float32x4_t vec) { return vcvtq_s32_f32(vec); }
FX_FORCE_INLINE float32x4_t ToFloat(uint32x4_t vec) { return vcvtq_f32_u32(vec); }
FX_FORCE_INLINE float32x4_t ToFloat(int32x4_t vec) { return vcvtq_f32_s32(vec); }

FX_FORCE_INLINE uint32x4_t ReinterpretAsUInt(float32x4_t vec) { return vreinterpretq_u32_f32(vec); }
FX_FORCE_INLINE uint32x4_t ReinterpretAsInt(float32x4_t vec) { return vreinterpretq_s32_f32(vec); }
FX_FORCE_INLINE float32x4_t ReinterpretAsFloat(uint32x4_t vec) { return vreinterpretq_f32_u32(vec); }
FX_FORCE_INLINE float32x4_t ReinterpretAsFloat(int32x4_t vec) { return vreinterpretq_f32_s32(vec); }

FX_FORCE_INLINE float32x4_t AndNot(float32x4_t a, float32x4_t b)
{
	return ReinterpretAsFloat(vbicq_u32(ReinterpretAsUInt(b), ReinterpretAsUInt(a)));
}

FX_FORCE_INLINE float32x4_t Negate(float32x4_t a)
{
	return ReinterpretAsFloat(veorq_u32(ReinterpretAsUInt(a), vdupq_n_u32(scSignMask32)));
}

FX_FORCE_INLINE float32x4_t Floor(float32x4_t vec) { return vrndmq_f32(vec); }

FX_FORCE_INLINE float32 Dot(float32x4_t a, float32x4_t b) { return vaddvq_f32(vmulq_f32(a, b)); }

FX_FORCE_INLINE float32x4_t Cross(float32x4_t a, float32x4_t b)
{
	float32x4_t a_yzxw = simd::Permute4<simd::Shuffle_AY, simd::Shuffle_AZ, simd::Shuffle_AX, simd::Shuffle_AW>(a);
	float32x4_t b_yzxw = simd::Permute4<simd::Shuffle_AY, simd::Shuffle_AZ, simd::Shuffle_AX, simd::Shuffle_AW>(b);

	const float32x4_t result_yzxw = vsubq_f32(vmulq_f32(a, b_yzxw), vmulq_f32(a_yzxw, b));

	return simd::Permute4<simd::Shuffle_AY, simd::Shuffle_AZ, simd::Shuffle_AX, simd::Shuffle_AW>(result_yzxw);
}


FX_FORCE_INLINE float32x4_t Xor(float32x4_t a, float32x4_t b)
{
	return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
}

FX_FORCE_INLINE float32x4_t Xor(float32x4_t a, uint32x4_t b)
{
	return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(a), b));
}

FX_FORCE_INLINE float32x4_t And(float32x4_t a, float32x4_t b)
{
	return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
}

/**
 * @brief Set the signs of all components of `v` to the sign of `TSign`.
 * @tparam TSign A value (e.g. 1.0 or -1.0) to specify the sign.
 */
template <int TSign>
FX_FORCE_INLINE float32x4_t SetSigns(float32x4_t v)
{
	// Same as -0.0f, only the sign bit
	const uint32x4_t sign_v = vdupq_n_u32(Neon::scSignMask32);

	// TSign is +ve,
	if constexpr (TSign > 0.0) {
		// We just return AND v, NOT sign
		// Missing the ol' SSE _mm_andnot_ps here
		return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(v), vmvnq_u32(sign_v)));
	}
	// TSign is -ve, add the sign bit if its not already there
	return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(v), sign_v));
}

template <int TX, int TY, int TZ, int TW>
FX_FORCE_INLINE float32x4_t FlipSigns(float32x4_t v)
{
	constexpr float32 signs[4] = {
		TX > 0 ? 0.0f : -0.0f,
		TY > 0 ? 0.0f : -0.0f,
		TZ > 0 ? 0.0f : -0.0f,
		TW > 0 ? 0.0f : -0.0f,
	};

	const float32x4_t sign_v = vld1q_f32(signs);
	return Xor(v, sign_v);
}


FX_FORCE_INLINE float32x4_t Sqrt(float32x4_t vec) { return vsqrtq_f32(vec); }

void SinCos4(float32x4_t x, float32x4_t* ysin, float32x4_t* ycos);

/**
 * @brief A low-accuracy but super fast SinCos implementation (about 0.06% error)
 */
void SinCos4_Fast(float32x4_t angles, float32x4_t* out_sine, float32x4_t* out_cosine);

}; // namespace fx::Neon

#endif // FX_USE_NEON
