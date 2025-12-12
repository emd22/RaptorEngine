#pragma once

#include <Core/FxDefines.hpp>

#ifdef FX_USE_SSE

#include <immintrin.h>


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

}; // namespace FxSSE

#endif