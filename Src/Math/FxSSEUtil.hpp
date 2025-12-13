#pragma once

#include <Core/FxDefines.hpp>

#ifdef FX_USE_SSE

#include <immintrin.h>

enum FxShuffleComponent
{
    /* A Vector */
    FxShuffle_AX = 0,
    FxShuffle_AY = 1,
    FxShuffle_AZ = 2,
    FxShuffle_AW = 3,

    /* B Vector */
    FxShuffle_BX = 4,
    FxShuffle_BY = 5,
    FxShuffle_BZ = 6,
    FxShuffle_BW = 7,
};

namespace FxSSE {

FX_FORCE_INLINE __m128 RemoveSign(__m128 vec)
{
    // Based off of https://fastcpp.blogspot.com/2011/03/changing-sign-of-float-values-using-sse.html
    
    const __m128 sign_mask = _mm_set1_ps(-0.0);
    return _mm_andnot_ps(vec, sign_mask);
}

template <int TX, int TY, int TZ, int TW>
FX_FORCE_INLINE __m128 SetSign(__m128 v)
{
    // [sign][exponent][  mantissa    ]
    // 
    // We can represent just the sign bit using -0.0f
    constexpr float32 signs[4] = {
        TX > 0 ? 0.0f : -0.0f,
        TY > 0 ? 0.0f : -0.0f,
        TZ > 0 ? 0.0f : -0.0f,
        TW > 0 ? 0.0f : -0.0f,
    };

    const __m128 sign_v = _mm_load_ps(signs);

    return _mm_xor_ps(v, sign_v);
}

template <FxShuffleComponent TComp1, FxShuffleComponent TComp2, FxShuffleComponent TComp3, FxShuffleComponent TComp4>
FX_FORCE_INLINE __m128 Permute4(__m128 a)
{
    // Assert that all components are for A
    static_assert(TComp1 < FxShuffle_BX);
    static_assert(TComp2 < FxShuffle_BX);
    static_assert(TComp3 < FxShuffle_BX);
    static_assert(TComp4 < FxShuffle_BX);

    constexpr uint8 permute = _MM_SHUFFLE(TComp4, TComp3, TComp2, TComp1);
    return _mm_shuffle_ps(a, a, permute);
}

}; // namespace FxSSE

#endif