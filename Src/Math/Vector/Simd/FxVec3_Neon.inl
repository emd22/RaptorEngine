#pragma once

#include <Math/FxVec3.hpp>

#ifdef FX_USE_NEON

FX_FORCE_INLINE bool FxVec3f::IsCloseTo(const FxVec3f& other, const float32 tolerance) const
{
    return IsCloseTo(other.mIntrin);
}

FX_FORCE_INLINE bool FxVec3f::IsCloseTo(const float32x4_t& other, const float32 tolerance) const
{
    // Get the absolute difference between the vectors
    const float32x4_t diff = vabdq_f32(mIntrin, other);

    // Is the difference greater than our threshhold?
    const uint32x4_t lt = vcgtq_f32(diff, vdupq_n_f32(tolerance));

    // If any components are true, return false.
    return vmaxvq_u32(lt) == 0;
}

FX_FORCE_INLINE bool FxVec3f::operator==(const FxVec3f& other) const
{
    return (vaddvq_u32(vceqq_f32(mIntrin, other.mIntrin))) != 0;
}

FX_FORCE_INLINE void FxVec3f::Set(float32 x, float32 y, float32 z)
{
    const float32 v[] = { x, y, z, 0 };
    mIntrin = vld1q_f32(v);
}

FX_FORCE_INLINE FxVec3f FxVec3f::Min(const FxVec3f& a, const FxVec3f& b)
{
    return FxVec3f(vminq_f32(a.mIntrin, b.mIntrin));
}

FX_FORCE_INLINE FxVec3f FxVec3f::Max(const FxVec3f& a, const FxVec3f& b)
{
    return FxVec3f(vmaxq_f32(a.mIntrin, b.mIntrin));
}


//////////////////////////////
// Operator Overloads
//////////////////////////////

FX_FORCE_INLINE FxVec3f FxVec3f::operator+(const FxVec3f& other) const
{
    float32x4_t result = vaddq_f32(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator-(const FxVec3f& other) const
{
    float32x4_t result = vsubq_f32(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator*(const FxVec3f& other) const
{
    float32x4_t result = vmulq_f32(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator*(float32 scalar) const
{
    float32x4_t result = vmulq_n_f32(mIntrin, scalar);
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator-() const
{
    float32x4_t result = vnegq_f32(mIntrin);
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator/(float32 scalar) const
{
    float32x4_t result = vdivq_f32(mIntrin, vdupq_n_f32(scalar));
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f& FxVec3f::operator+=(const FxVec3f& other)
{
    mIntrin = vaddq_f32(mIntrin, other);
    return *this;
}

FX_FORCE_INLINE FxVec3f& FxVec3f::operator-=(const FxVec3f& other)
{
    mIntrin = vsubq_f32(mIntrin, other);
    return *this;
}

FX_FORCE_INLINE FxVec3f& FxVec3f::operator*=(const FxVec3f& other)
{
    mIntrin = vmulq_f32(mIntrin, other);
    return *this;
}


#endif
