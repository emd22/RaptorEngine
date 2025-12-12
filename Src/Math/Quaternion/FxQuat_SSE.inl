#pragma once

#include <Math/FxQuat.hpp>

#ifdef FX_USE_SSE

#include "FxSSEUtil.hpp"


FX_FORCE_INLINE bool FxQuat::IsCloseTo(const __m128 other, const float32 tolerance) const
{
    // TODO: Would this be cheaper to use _mm_dp_ps?
    
    // Get the absolute difference
    const __m128 diff = FxSSE::RemoveSign(_mm_sub_ps(mIntrin, other));

    // Check if any absolute difference is > tolerance
    __m128i cmpop = _mm_castps_si128(_mm_cmpgt_ps(diff, _mm_set1_ps(tolerance)));
    
    // If any components are, return true
    return static_cast<bool>(_mm_testz_si128(cmpop, cmpop));
}


#endif