#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_NEON

#include <arm_neon.h>
#include <math.h>

#include <Core/Types.hpp>

namespace fx::Neon {

enum ShuffleComponent
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

template <ShuffleComponent TA, ShuffleComponent TB, ShuffleComponent TC, ShuffleComponent TD>
concept C_SinglePermute = (TA <= Shuffle_AW) && (TB <= Shuffle_AW) && (TC <= Shuffle_AW) && (TD <= Shuffle_AW);


template <ShuffleComponent TA, ShuffleComponent TB, ShuffleComponent TC, ShuffleComponent TD>
concept C_SinglePermuteForB = (TA >= Shuffle_BX) && (TB >= Shuffle_BX) && (TC >= Shuffle_BX) && (TD >= Shuffle_BX);


template <ShuffleComponent TA, ShuffleComponent TB, ShuffleComponent TC, ShuffleComponent TD>
concept C_AllComponentsSame = (TA == TB == TC == TD);

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

////////////////////////////////
// Permute/Reorder Functions
////////////////////////////////

/**
 * @brief Returns a single vector rearranged to the template parameter values.
 */
template <ShuffleComponent TComp1, ShuffleComponent TComp2, ShuffleComponent TComp3, ShuffleComponent TComp4>
FX_FORCE_INLINE float32x4_t Permute4(float32x4_t a)
    requires C_SinglePermute<TComp1, TComp2, TComp3, TComp4>
{
    // Unspecialized shuffle, reorder the lanes directly

    float32x4_t result;
    result = vmovq_n_f32(vgetq_lane_f32(a, TComp1 & 0x03));
    result = vsetq_lane_f32(vgetq_lane_f32(a, TComp2 & 0x03), result, 1);
    result = vsetq_lane_f32(vgetq_lane_f32(a, TComp3 & 0x03), result, 2);
    result = vsetq_lane_f32(vgetq_lane_f32(a, TComp4 & 0x03), result, 3);

    return result;
}

// Specializations


template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AW, Shuffle_AZ, Shuffle_AY, Shuffle_AX>(float32x4_t a)
{
    return vcombine_f32(vrev64_f32(vget_high_f32(a)), vrev64_f32(vget_low_f32(a)));
}

template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AX, Shuffle_AY, Shuffle_AZ, Shuffle_AW>(float32x4_t a)
{
    return a;
}

template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AX, Shuffle_AY, Shuffle_AZ, Shuffle_AZ>(float32x4_t a)
{
    return vcombine_f32(vget_low_f32(a), vdup_lane_f32(vget_high_f32(a), 0));
}

template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AX, Shuffle_AY, Shuffle_AW, Shuffle_AW>(float32x4_t a)
{
    return vcombine_f32(vget_low_f32(a), vdup_lane_f32(vget_high_f32(a), 1));
}


template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AY, Shuffle_AX, Shuffle_AW, Shuffle_AZ>(float32x4_t a)
{
    return vcombine_f32(vrev64_f32(vget_low_f32(a)), vrev64_f32(vget_high_f32(a)));
}

template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AZ, Shuffle_AZ, Shuffle_AY, Shuffle_AX>(float32x4_t a)
{
    return vcombine_f32(vdup_lane_f32(vget_high_f32(a), 0), vrev64_f32(vget_low_f32(a)));
}

template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AZ, Shuffle_AW, Shuffle_AX, Shuffle_AY>(float32x4_t a)
{
    return vcombine_f32(vget_high_f32(a), vget_low_f32(a));
}

template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AX>(float32x4_t a)
{
    static uint8x16_t table = { 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03 };
    return vreinterpretq_f32_u8(vqtbl1q_u8(vreinterpretq_u8_f32(a), table));
}

/**
 * @brief Returns the vector A rearranged to the components {Y, Z, X, W}.
 */
template <>
FX_FORCE_INLINE float32x4_t Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(float32x4_t a)
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

template <ShuffleComponent TComp1, ShuffleComponent TComp2, ShuffleComponent TComp3, ShuffleComponent TComp4>
    requires C_AllComponentsSame<TComp1, TComp2, TComp3, TComp4>
FX_FORCE_INLINE float32x4_t Permute4(float32x4_t a)
{
    return vdupq_laneq_f32(a, TComp1);
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
template <ShuffleComponent TComp1, ShuffleComponent TComp2, ShuffleComponent TComp3, ShuffleComponent TComp4>
FX_FORCE_INLINE float32x4_t Permute4(float32x4_t a, float32x4_t b)
{
    // Unspecialized shuffle, reorder the lanes directly

    float32x4_t result;
    result = vmovq_n_f32(vgetq_lane_f32(TComp1 >= Shuffle_BX ? b : a, TComp1 & 0x03));
    result = vsetq_lane_f32(vgetq_lane_f32(TComp2 >= Shuffle_BX ? b : a, TComp2 & 0x03), result, 1);
    result = vsetq_lane_f32(vgetq_lane_f32(TComp3 >= Shuffle_BX ? b : a, TComp3 & 0x03), result, 2);
    result = vsetq_lane_f32(vgetq_lane_f32(TComp3 >= Shuffle_BX ? b : a, TComp4 & 0x03), result, 3);

    return result;
}

/**
 * @brief Returns the single permutation of the two vectors A and B, using the components passed in via template
 * parameters.
 */
template <ShuffleComponent TComp1, ShuffleComponent TComp2, ShuffleComponent TComp3, ShuffleComponent TComp4>
FX_FORCE_INLINE float32x4_t Permute4(float32x4_t a, float32x4_t b)
    requires C_SinglePermute<TComp1, TComp2, TComp3, TComp4>
{
    return Permute4<TComp1, TComp2, TComp3, TComp4>(a);
}

/**
 * @brief Returns the vector B shuffled to the order passed in from the template arguments.
 */
template <ShuffleComponent TComp1, ShuffleComponent TComp2, ShuffleComponent TComp3, ShuffleComponent TComp4>
FX_FORCE_INLINE float32x4_t Permute4(float32x4_t a, float32x4_t b)
    requires C_SinglePermuteForB<TComp1, TComp2, TComp3, TComp4>
{
    // Adjust indices for single permutations
    return Permute4<TComp1 - Shuffle_BX, TComp2 - Shuffle_BX, TComp3 - Shuffle_BX, TComp4 - Shuffle_BX>(b);
}

FX_FORCE_INLINE float32 Dot(float32x4_t a, float32x4_t b) { return vaddvq_f32(vmulq_f32(a, b)); }

FX_FORCE_INLINE float32x4_t Cross(float32x4_t a, float32x4_t b)
{
    float32x4_t a_yzxw = Neon::Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(a);
    float32x4_t b_yzxw = Neon::Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(b);

    const float32x4_t result_yzxw = vsubq_f32(vmulq_f32(a, b_yzxw), vmulq_f32(a_yzxw, b));

    return Neon::Permute4<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(result_yzxw);
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
