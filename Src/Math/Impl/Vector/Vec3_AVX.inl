#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <Math/SSE.hpp>
#include <Math/Vec3.hpp>

namespace fx {

FX_FORCE_INLINE Vec3f::Vec3f(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values alignas(16)[4] = { x, y, z, w };
    mIntrin = _mm_load_ps(values);
}

FX_FORCE_INLINE Vec3f::Vec3f(const float32* unaligned)
{
    // Allocate here to avoid unordered loads into our SSE register and avoid overstepping the buffer
    const float32 values alignas(16)[4] = { unaligned[0], unaligned[1], unaligned[2], 0 };
    mIntrin = _mm_load_ps(values);
}

FX_FORCE_INLINE Vec3f::Vec3f(float32 scalar) { mIntrin = _mm_set1_ps(scalar); }

FX_FORCE_INLINE bool Vec3f::IsCloseTo(const Vec3f& other, const float32 tolerance) const
{
    return IsCloseTo(other.mIntrin, tolerance);
}

FX_FORCE_INLINE Vec3f Vec3f::MulAdd(const Vec3f& a, const Vec3f& b, const Vec3f& accum)
{
    return Vec3f(_mm_fmadd_ps(a.mIntrin, b.mIntrin, accum.mIntrin));
}

FX_FORCE_INLINE bool Vec3f::IsCloseTo(const Vec3f::SimdType other, const float32 tolerance) const
{
    // Get the absolute difference
    const __m128 diff = SSE::RemoveSign(_mm_sub_ps(mIntrin, other));

    // Check if any absolute difference is > tolerance
    __m128i cmp_v = _mm_castps_si128(_mm_cmpgt_ps(diff, _mm_set1_ps(tolerance)));

    // If any components are, return true
    return static_cast<bool>(_mm_testz_si128(cmp_v, cmp_v));
}

FX_FORCE_INLINE float32 Vec3f::Dot(const Vec3f& other) const { return Dot(other.mIntrin); }

FX_FORCE_INLINE bool Vec3f::IsNearZero(const float32 tolerance) const { return IsCloseTo(sZero, tolerance); }

FX_FORCE_INLINE void Vec3f::Set(float32 x, float32 y, float32 z) { mIntrin = _mm_setr_ps(x, y, z, 0.0f); }

FX_FORCE_INLINE float32 Vec3f::Dot(Vec3f::SimdType other) const
{
    // Mask is Src->0111 Dest->1111 so we do not include the unused component in our result
    return _mm_cvtss_f32(_mm_dp_ps(mIntrin, other, 0x7F));
}

FX_FORCE_INLINE Vec3f Vec3f::Min(const Vec3f& a, const Vec3f& b) { return Vec3f(_mm_min_ps(a.mIntrin, b.mIntrin)); }

FX_FORCE_INLINE Vec3f Vec3f::Max(const Vec3f& a, const Vec3f& b) { return Vec3f(_mm_max_ps(a.mIntrin, b.mIntrin)); }

FX_FORCE_INLINE Vec3f Vec3f::Clamp(const Vec3f& v, const Vec3f& min, const Vec3f& max)
{
    return Vec3f(_mm_min_ps(_mm_max_ps(v, min.mIntrin), max.mIntrin));
}

FX_FORCE_INLINE Vec3f Vec3f::Lerp(const Vec3f& a, const Vec3f& b, const float f)
{
    // a + f * (b - a);
    __m128 d = _mm_sub_ps(b, a);
    const __m128 f_v = _mm_set1_ps(f);

    return Vec3f(_mm_add_ps(a, _mm_mul_ps(d, f_v)));
}


FX_FORCE_INLINE Vec3f& Vec3f::LerpIP(const Vec3f& dest, const float step)
{
    // a + f * (b - a);
    __m128 d = _mm_sub_ps(dest, mIntrin);
    const __m128 f_v = _mm_set1_ps(step);

    mIntrin = _mm_add_ps(mIntrin, _mm_mul_ps(d, f_v));

    return *this;
}

FX_FORCE_INLINE Vec3f& Vec3f::NormalizeIP()
{
    const float32 length = Length();

    if (length == 0.0f) {
        return *this;
    }

    // Calculate length and splat to register
    const __m128 len_v = _mm_set1_ps(Length());

    // Divide vector by length
    mIntrin = _mm_div_ps(mIntrin, len_v);

    return *this;
}

FX_FORCE_INLINE Vec3f Vec3f::Normalize() const
{
    const float32 length = Length();

    if (length == 0.0f) {
        return *this;
    }

    // Calculate length and splat to register
    const __m128 len_v = _mm_set1_ps(length);

    return Vec3f(_mm_div_ps(mIntrin, len_v));
}

FX_FORCE_INLINE float32 Vec3f::Length() const
{
    __m128 v = mIntrin;
    return sqrtf(Dot(v));
}


//////////////////////////////
// Operator Overloads
//////////////////////////////

FX_FORCE_INLINE bool Vec3f::operator==(const Vec3f& other) const
{
    __m128i cmp_v = _mm_castps_si128(_mm_cmpeq_ps(mIntrin, other.mIntrin));
    // This is frequently done as a comparision against _mm_movemask, but _mm_test_all_ones potentially saves one extra
    // instruction.
    return static_cast<bool>(_mm_test_all_ones(cmp_v));
}

FX_FORCE_INLINE Vec3f Vec3f::operator+(const Vec3f& other) const
{
    __m128 result = _mm_add_ps(mIntrin, other.mIntrin);
    return Vec3f(result);
}

FX_FORCE_INLINE Vec3f Vec3f::operator-(const Vec3f& other) const
{
    __m128 result = _mm_sub_ps(mIntrin, other.mIntrin);
    return Vec3f(result);
}

FX_FORCE_INLINE Vec3f Vec3f::operator*(const Vec3f& other) const
{
    __m128 result = _mm_mul_ps(mIntrin, other.mIntrin);
    return Vec3f(result);
}

FX_FORCE_INLINE Vec3f Vec3f::operator/(const Vec3f& other) const
{
    __m128 result = _mm_div_ps(mIntrin, other.mIntrin);
    return Vec3f(result);
}

FX_FORCE_INLINE Vec3f Vec3f::operator*(float32 scalar) const
{
    __m128 result = _mm_mul_ps(mIntrin, _mm_set1_ps(scalar));
    return Vec3f(result);
}


FX_FORCE_INLINE Vec3f Vec3f::operator/(float32 scalar) const
{
    __m128 result = _mm_div_ps(mIntrin, _mm_set1_ps(scalar));
    return Vec3f(result);
}


FX_FORCE_INLINE Vec3f Vec3f::operator-() const
{
    __m128 result = _mm_xor_ps(mIntrin, _mm_set1_ps(-0.0f));
    return Vec3f(result);
}


FX_FORCE_INLINE Vec3f& Vec3f::operator+=(const Vec3f& other)
{
    mIntrin = _mm_add_ps(mIntrin, other.mIntrin);
    return *this;
}

FX_FORCE_INLINE Vec3f& Vec3f::operator-=(const Vec3f& other)
{
    mIntrin = _mm_sub_ps(mIntrin, other.mIntrin);
    return *this;
}

FX_FORCE_INLINE Vec3f& Vec3f::operator-=(float32 scalar)
{
    mIntrin = _mm_sub_ps(mIntrin, _mm_set1_ps(scalar));
    return *this;
}

FX_FORCE_INLINE Vec3f& Vec3f::operator*=(const Vec3f& other)
{
    mIntrin = _mm_mul_ps(mIntrin, other.mIntrin);
    return *this;
}

} // namespace fx

#endif // #ifdef FX_USE_AVX
