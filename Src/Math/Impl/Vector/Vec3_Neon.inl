#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_NEON

#include <Math/Vec3.hpp>

namespace fx {

FX_FORCE_INLINE Vec3f::Vec3f(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = { x, y, z, w };
    mIntrin = vld1q_f32(values);
}

FX_FORCE_INLINE Vec3f::Vec3f(const float32* values)
{
    const float32 values4[4] = { values[0], values[1], values[2], 0 };
    mIntrin = vld1q_f32(values4);
}

FX_FORCE_INLINE Vec3f::Vec3f(float32 scalar) { mIntrin = vdupq_n_f32(scalar); }

FX_FORCE_INLINE bool Vec3f::IsCloseTo(const Vec3f& other, const float32 tolerance) const
{
    return IsCloseTo(other.mIntrin, tolerance);
}

FX_FORCE_INLINE Vec3f Vec3f::MulAdd(const Vec3f& a, const Vec3f& b, const Vec3f& accum)
{
    return Vec3f(vfmaq_f32(accum.mIntrin, a.mIntrin, b.mIntrin));
}


FX_FORCE_INLINE bool Vec3f::IsCloseTo(const Vec3f::SimdType other, const float32 tolerance) const
{
    // Get the absolute difference between the vectors
    const float32x4_t diff = vabdq_f32(mIntrin, other);

    // Is the difference greater than our threshold?
    const uint32x4_t lt = vcgtq_f32(diff, vdupq_n_f32(tolerance));

    // If any components are true, return false.
    return vmaxvq_u32(lt) == 0;
}

FX_FORCE_INLINE bool Vec3f::IsZero() const
{
    const uint32x4_t is_zero_v = vceqzq_f32(mIntrin);
    return vmaxvq_u32(is_zero_v) == 0;
}

FX_FORCE_INLINE bool Vec3f::IsNearZero(const float32 tolerance) const { return IsCloseTo(sZero, tolerance); }


FX_FORCE_INLINE void Vec3f::Set(float32 x, float32 y, float32 z)
{
    const float32 v[] = { x, y, z, 0 };
    mIntrin = vld1q_f32(v);
}

FX_FORCE_INLINE Vec3f Vec3f::Min(const Vec3f& a, const Vec3f& b) { return Vec3f(vminq_f32(a.mIntrin, b.mIntrin)); }

FX_FORCE_INLINE Vec3f Vec3f::Max(const Vec3f& a, const Vec3f& b) { return Vec3f(vmaxq_f32(a.mIntrin, b.mIntrin)); }

FX_FORCE_INLINE Vec3f Vec3f::Clamp(const Vec3f& v, const Vec3f& min, const Vec3f& max)
{
    return Vec3f(vminq_f32(vmaxq_f32(v, min.mIntrin), max.mIntrin));
}

FX_FORCE_INLINE Vec3f Vec3f::Lerp(const Vec3f& a, const Vec3f& b, const float f)
{
    // a + f * (b - a);
    float32x4_t d = vsubq_f32(b, a);

    return Vec3f(vaddq_f32(a, vmulq_n_f32(d, f)));
}

FX_FORCE_INLINE Vec3f& Vec3f::LerpIP(const Vec3f& dest, const float step)
{
    // a + f * (b - a);
    float32x4_t d = vsubq_f32(dest, mIntrin);

    mIntrin = vaddq_f32(mIntrin, vmulq_n_f32(d, step));

    return *this;
}


FX_FORCE_INLINE Vec3f& Vec3f::NormalizeIP()
{
    // Calculate length and splat to register
    const float32x4_t len_v = vdupq_n_f32(Length());

    // Divide vector by length
    mIntrin = vdivq_f32(mIntrin, len_v);

    return *this;
}

FX_FORCE_INLINE Vec3f Vec3f::Normalize() const
{
    // Calculate length and splat to register
    const float32x4_t len_v = vdupq_n_f32(Length());

    return Vec3f(vdivq_f32(mIntrin, len_v));
}


FX_FORCE_INLINE float32 Vec3f::Length() const
{
    float32x4_t v = mIntrin;
    return sqrtf(Dot(v));
}

FX_FORCE_INLINE float32 Vec3f::Dot(const Vec3f& other) const { return Neon::Dot(mIntrin, other.mIntrin); }

FX_FORCE_INLINE float32 Vec3f::Dot(Vec3f::SimdType other) const { return Neon::Dot(mIntrin, other); }

//////////////////////////////
// Operator Overloads
//////////////////////////////

FX_FORCE_INLINE bool Vec3f::operator==(const Vec3f& other) const
{
    return (vaddvq_u32(vceqq_f32(mIntrin, other.mIntrin))) != 0;
}

FX_FORCE_INLINE Vec3f Vec3f::operator+(const Vec3f& other) const { return Vec3f(vaddq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE Vec3f Vec3f::operator-(const Vec3f& other) const { return Vec3f(vsubq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE Vec3f Vec3f::operator*(const Vec3f& other) const { return Vec3f(vmulq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE Vec3f Vec3f::operator*(float32 scalar) const
{
    float32x4_t result = vmulq_n_f32(mIntrin, scalar);
    return Vec3f(result);
}

FX_FORCE_INLINE Vec3f Vec3f::operator/(const Vec3f& other) const { return Vec3f(vdivq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE Vec3f Vec3f::operator/(float32 scalar) const
{
    float32x4_t result = vdivq_f32(mIntrin, vdupq_n_f32(scalar));
    return Vec3f(result);
}

FX_FORCE_INLINE Vec3f Vec3f::operator-() const
{
    float32x4_t result = vnegq_f32(mIntrin);
    return Vec3f(result);
}


FX_FORCE_INLINE Vec3f& Vec3f::operator+=(const Vec3f& other)
{
    mIntrin = vaddq_f32(mIntrin, other);
    return *this;
}

FX_FORCE_INLINE Vec3f& Vec3f::operator-=(const Vec3f& other)
{
    mIntrin = vsubq_f32(mIntrin, other);
    return *this;
}

FX_FORCE_INLINE Vec3f& Vec3f::operator-=(float32 scalar)
{
    mIntrin = vsubq_f32(mIntrin, vdupq_n_f32(scalar));
    return *this;
}

FX_FORCE_INLINE Vec3f& Vec3f::operator*=(const Vec3f& other)
{
    mIntrin = vmulq_f32(mIntrin, other);
    return *this;
}

} // namespace fx

#endif
