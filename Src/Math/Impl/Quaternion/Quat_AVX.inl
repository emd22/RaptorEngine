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

FX_FORCE_INLINE Vec3f Quat::GetDirection() const
{
    // dir X = (2.0 * ((X * Z) + (Y * W)))
    // dir Y = (2.0 * ((Y * Z) - (X * W)))
    // dir Z = 1.0 - (2.0 * ((X * X) + (Y * Y)))

    const float32 dir_x = 2.0f * ((X * Z) + (Y * W));
    const float32 dir_y = 2.0f * ((Y * Z) - (X * W));
    const float32 dir_z = 1.0f - (2.0f * ((X * X) + (Y * Y)));

    return Vec3f(dir_x, dir_y, dir_z);
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
	float32 out[4];
	rx_quat_nlerp(&mData[0], &dest.mData[0], time, out);

	*this = Quat(out[0], out[1], out[2], out[3]);
}


// Based off of https://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/slerp/index.htm and optimized
// for Neon.
Quat Quat::SLerp(const Quat& dest, const float32 step) const
{
	float32 out[4];
	rx_quat_slerp(&mData[0], &dest.mData[0], step, out);

	return Quat(out[0], out[1], out[2], out[3]);
}

FX_FORCE_INLINE Quat Quat::Conjugate() const { return Quat(SSE::FlipSigns<-1, -1, -1, 1>(mIntrin)); }
FX_FORCE_INLINE Quat Quat::Normalize() const { return Quat(SSE::Normalize(mIntrin)); }

} // namespace fx

#endif
