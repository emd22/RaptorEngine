#pragma once

#include <Core/FxDefines.hpp>

#ifdef FX_USE_AVX

#include <Math/FxQuat.hpp>
#include <Math/FxAVXUtil.hpp>

FX_FORCE_INLINE FxQuat& FxQuat::operator=(const __m128 other)
{
    mIntrin = other;
    return *this;
}

FX_FORCE_INLINE bool FxQuat::IsCloseTo(const FxQuat& other, const float32 tolerance) const
{
    return IsCloseTo(other.mIntrin, tolerance);
}

FX_FORCE_INLINE bool FxQuat::IsCloseTo(const __m128 other, const float32 tolerance) const
{
    // Get the absolute difference
    const __m128 diff = FxSSE::RemoveSign(_mm_sub_ps(mIntrin, other));

    // Check if any absolute difference is > tolerance
    __m128i cmp_v = _mm_castps_si128(_mm_cmpgt_ps(diff, _mm_set1_ps(tolerance)));
    
    // If any components are, return true
    return static_cast<bool>(_mm_testz_si128(cmp_v, cmp_v));
}

//FX_FORCE_INLINE void FxQuat::LerpIP(const FxQuat& dest, float32 step)
//{
//    const __m128 inv_step_v = _mm_set1_ps(1.0 - step);
//    const __m128 step_v = _mm_set1_ps(step);
//
//    mIntrin = _mm_add_ps(_mm_mul_ps(inv_step_v, mIntrin), _mm_mul_ps(step_v, dest.mIntrin));
//}

FX_FORCE_INLINE void FxQuat::NLerpIP(const FxQuat& dest, float32 time)
{ 
    __m128 dest_v = dest.mIntrin;

    if (FxSSE::Dot(mIntrin, dest.mIntrin) < 0.0f) {
        dest_v = FxSSE::SetSigns<-1>(dest_v);
    }

    const __m128 inv_time_v = _mm_set1_ps(1.0 - time);
    const __m128 time_v = _mm_set1_ps(time);

    // (1 - time) * A
    mIntrin = _mm_mul_ps(inv_time_v, mIntrin);

    //  ... + time * B  ->  time * dest + mIntrin
    mIntrin = _mm_fmadd_ps(time_v, dest_v, mIntrin);

    mIntrin = FxSSE::Normalize(mIntrin);
}


#endif