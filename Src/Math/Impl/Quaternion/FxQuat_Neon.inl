#pragma once

#include <Math/FxQuat.hpp>

#ifdef FX_USE_NEON

FX_FORCE_INLINE FxQuat& FxQuat::operator=(const float32x4_t& other)
{
    mIntrin = other;
    return *this;
}

FX_FORCE_INLINE bool FxQuat::IsCloseTo(const FxQuat& other, const float32 tolerance) const
{
    return IsCloseTo(other.mIntrin, tolerance);
}

FX_FORCE_INLINE bool FxQuat::IsCloseTo(const float32x4_t& other, const float32 tolerance) const
{
    // Get the absolute difference between the vectors
    const float32x4_t diff = vabdq_f32(mIntrin, other);

    // Is the difference greater than our threshhold?
    const uint32x4_t lt = vcgtq_f32(diff, vdupq_n_f32(tolerance));

    // If any components are true, return false.
    return vmaxvq_u32(lt) == 0;
}

FX_FORCE_INLINE void FxQuat::NLerpIP(const FxQuat& dest, float32 time)
{
    // We can change (A + (B - A) * time) to ((1 - time) * A + time * B) to reduce floating point errors.

    float32x4_t dest_v = dest.mIntrin;

    if (FxNeon::Dot(mIntrin, dest.mIntrin) < 0.0f) {
        dest_v = FxNeon::SetSigns<-1>(dest_v);
    }

    const float32x4_t inv_time_v = vdupq_n_f32(1.0 - time);
    const float32x4_t time_v = vdupq_n_f32(time);

    // (1 - time) * A
    mIntrin = vmulq_f32(inv_time_v, mIntrin);
    // ... + time * B -> vfmaq takes in (D, N, M); N and M are multiplied, and accumulated to D(destination).
    mIntrin = vfmaq_f32(mIntrin, time_v, dest_v);

    // Quaternion lerp is always normalized!
    mIntrin = FxNeon::Normalize(mIntrin);
}

FX_FORCE_INLINE void FxQuat::SLerpIP(const FxQuat& dest, float32 step)
{
    // Dot product
}

#endif
