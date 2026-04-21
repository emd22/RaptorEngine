#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <Math/Quat.hpp>
#include <Math/SSE.hpp>
#include <Math/SSEUtil.hpp>

namespace fx {

FX_FORCE_INLINE Quat& Quat::operator=(const __m128 other)
{
    mIntrin = other;
    return *this;
}

FX_FORCE_INLINE Quat::Quat(const float32* buffer)
{
    const float32 aligned_buffer[] = { buffer[0], buffer[1], buffer[2], buffer[3] };
    mIntrin = _mm_load_ps(aligned_buffer);
}

FX_FORCE_INLINE bool Quat::IsCloseTo(const Quat& other, const float32 tolerance) const
{
    return IsCloseTo(other.mIntrin, tolerance);
}

FX_FORCE_INLINE bool Quat::IsCloseTo(const __m128 other, const float32 tolerance) const
{
    // Get the absolute difference
    const __m128 diff = SSE::RemoveSign(_mm_sub_ps(mIntrin, other));

    // Check if any absolute difference is > tolerance
    __m128i cmp_v = _mm_castps_si128(_mm_cmpgt_ps(diff, _mm_set1_ps(tolerance)));

    // If any components are, return true
    return static_cast<bool>(_mm_testz_si128(cmp_v, cmp_v));
}

// FX_FORCE_INLINE void Quat::LerpIP(const Quat& dest, float32 step)
//{
//     const __m128 inv_step_v = _mm_set1_ps(1.0 - step);
//     const __m128 step_v = _mm_set1_ps(step);
//
//     mIntrin = _mm_add_ps(_mm_mul_ps(inv_step_v, mIntrin), _mm_mul_ps(step_v, dest.mIntrin));
// }

FX_FORCE_INLINE void Quat::NLerpIP(const Quat& dest, float32 time)
{
    __m128 dest_v = dest.mIntrin;

    if (SSE::Dot(mIntrin, dest.mIntrin) < 0.0f) {
        dest_v = SSE::SetSigns<-1>(dest_v);
    }

    const __m128 inv_time_v = _mm_set1_ps(1.0 - time);
    const __m128 time_v = _mm_set1_ps(time);

    // (1 - time) * A
    mIntrin = _mm_mul_ps(inv_time_v, mIntrin);

    //  ... + time * B  ->  time * dest + mIntrin
    mIntrin = _mm_fmadd_ps(time_v, dest_v, mIntrin);

    mIntrin = SSE::Normalize(mIntrin);
}


// Based off of https://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/slerp/index.htm and optimized
// for Neon.
Quat Quat::SLerp(const Quat& dest, const float32 step) const
{
    // Note: there are so many different ways to implement this and a lot of them online just straight up pro
    __m128 a_v = mIntrin;
    __m128 b_v = dest.mIntrin;

    __m128 result;

    // Calculate angle between them.
    float32 cos_half_theta = SSE::Dot(a_v, b_v);
    // if qa=qb or qa=-qb then theta = 0 and we can return qa
    if (abs(cos_half_theta) >= 1.0) {
        return Quat(mIntrin);
    }

    // Calculate temporary values.
    float32 half_theta = acosf(cos_half_theta);
    float32 sin_half_theta = sqrtf(1.0f - cos_half_theta * cos_half_theta);

    // if theta = 180 degrees then result is not fully defined
    // we could rotate around any axis normal to qa or qb
    if (sin_half_theta < 0.001) {
        __m128 half = _mm_set1_ps(0.5f);

        result = _mm_mul_ps(a_v, half);
        result = _mm_fmadd_ps(b_v, half, result);

        return Quat(result);
    }

    const float32 sht_recip = 1.0f / sin_half_theta;
    float32 ratioA = sinf((1.0f - step) * half_theta) * sht_recip;
    float32 ratioB = sinf(step * half_theta) * sht_recip;

    result = _mm_mul_ps(a_v, _mm_set1_ps(ratioA));
    result = _mm_fmadd_ps(b_v, _mm_set1_ps(ratioB), result);

    return Quat(result);
}

FX_FORCE_INLINE Quat Quat::Conjugate() const { return Quat(SSE::FlipSigns<-1, -1, -1, 1>(mIntrin)); }

} // namespace fx

#endif
