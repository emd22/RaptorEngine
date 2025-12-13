#pragma once

#include <Math/FxQuat.hpp>

#ifdef FX_USE_SSE

#include <Math/FxSSEUtil.hpp>

FX_FORCE_INLINE bool FxQuat::IsCloseTo(const __m128 other, const float32 tolerance) const
{
    // Get the absolute difference
    const __m128 diff = FxSSE::RemoveSign(_mm_sub_ps(mIntrin, other));

    // Check if any absolute difference is > tolerance
    __m128i cmp_v = _mm_castps_si128(_mm_cmpgt_ps(diff, _mm_set1_ps(tolerance)));
    
    // If any components are, return true
    return static_cast<bool>(_mm_testz_si128(cmp_v, cmp_v));
}


#endif