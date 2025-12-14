#pragma once

#include <Core/FxDefines.hpp>

#ifdef FX_USE_AVX

#include <Math/FxVec3.hpp>
#include <immintrin.h>

FX_FORCE_INLINE bool FxVec3f::IsCloseTo(const FxVec3f& other, const float32 tolerance) const
{
    return IsCloseTo(other.mIntrin);
}

FX_FORCE_INLINE bool FxVec3f::IsCloseTo(const FxVec3f::SimdType other, const float32 tolerance) const
{
    // Get the absolute difference
    const __m128 diff = FxSSE::RemoveSign(_mm_sub_ps(mIntrin, other));

    // Check if any absolute difference is > tolerance
    __m128i cmp_v = _mm_castps_si128(_mm_cmpgt_ps(diff, _mm_set1_ps(tolerance)));
    
    // If any components are, return true
    return static_cast<bool>(_mm_testz_si128(cmp_v, cmp_v));
}

FX_FORCE_INLINE float32 FxVec3f::Dot(const FxVec3f& other) const
{
    return _mm_cvtss_f32(_mm_dp_ps(mIntrin, other.mIntrin, 0xFF));
}

FX_FORCE_INLINE bool FxVec3f::IsNearZero(const float32 tolerance) const { return IsCloseTo(sZero, tolerance); }

FX_FORCE_INLINE bool FxVec3f::operator==(const FxVec3f& other) const
{
    __m128i cmp_v = _mm_castps_si128(_mm_cmpeq_ps(mIntrin, other.mIntrin));
    return static_cast<bool>(_mm_test_all_ones(cmp_v));
}

FX_FORCE_INLINE void FxVec3f::Set(float32 x, float32 y, float32 z)
{
    const float32 v[] = { x, y, z, 0 };
    mIntrin = _mm_load_ps(v);
}



FX_FORCE_INLINE float32 FxVec3f::Dot(FxVec3f::SimdType other) const 
{
     return _mm_cvtss_f32(_mm_dp_ps(mIntrin, other, 0xFF));
}

FX_FORCE_INLINE FxVec3f FxVec3f::Min(const FxVec3f& a, const FxVec3f& b)
{
    return FxVec3f(_mm_min_ps(a.mIntrin, b.mIntrin));
}

FX_FORCE_INLINE FxVec3f FxVec3f::Max(const FxVec3f& a, const FxVec3f& b)
{
    return FxVec3f(_mm_max_ps(a.mIntrin, b.mIntrin));
}

FX_FORCE_INLINE FxVec3f FxVec3f::Clamp(const FxVec3f& v, const FxVec3f& min, const FxVec3f& max)
{
    return FxVec3f(_mm_min_ps(_mm_max_ps(v, min.mIntrin), max.mIntrin));
}

FX_FORCE_INLINE FxVec3f FxVec3f::Lerp(const FxVec3f& a, const FxVec3f& b, const float f)
{
    // a + f * (b - a);
    __m128 d = _mm_sub_ps(b, a);

    const __m128 f_v = _mm_set1_ps(f);

    return FxVec3f(_mm_add_ps(a, _mm_mul_ps(d, f_v)));
}

FX_FORCE_INLINE FxVec3f& FxVec3f::NormalizeIP()
{
    // Calculate length and splat to register
    const __m128 len_v = _mm_set1_ps(Length());

    // Divide vector by length
    mIntrin = _mm_div_ps(mIntrin, len_v);

    return *this;
}

FX_FORCE_INLINE FxVec3f FxVec3f::Normalize() const
{
    // Calculate length and splat to register
    const __m128 len_v = _mm_set1_ps(Length());

    return FxVec3f(_mm_div_ps(mIntrin, len_v));
}

FX_FORCE_INLINE float32 FxVec3f::Length() const
{
    __m128 v = mIntrin;
    return sqrtf(Dot(v));
}


//////////////////////////////
// Operator Overloads
//////////////////////////////

FX_FORCE_INLINE FxVec3f FxVec3f::operator+(const FxVec3f& other) const
{
    __m128 result = _mm_add_ps(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator-(const FxVec3f& other) const
{
    __m128 result = _mm_sub_ps(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator*(const FxVec3f& other) const
{
    __m128 result = _mm_mul_ps(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator*(float32 scalar) const
{
    __m128 result = _mm_mul_ps(mIntrin, _mm_set1_ps(scalar));
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator-() const
{
    __m128 result = _mm_xor_ps(mIntrin, _mm_set1_ps(-0.0f));
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f FxVec3f::operator/(float32 scalar) const
{
    __m128 result = _mm_div_ps(mIntrin, _mm_set1_ps(scalar));
    return FxVec3f(result);
}

FX_FORCE_INLINE FxVec3f& FxVec3f::operator+=(const FxVec3f& other)
{
    mIntrin = _mm_add_ps(mIntrin, other);
    return *this;
}

FX_FORCE_INLINE FxVec3f& FxVec3f::operator-=(const FxVec3f& other)
{
    mIntrin = _mm_sub_ps(mIntrin, other);
    return *this;
}

FX_FORCE_INLINE FxVec3f& FxVec3f::operator-=(float32 scalar)
{
    mIntrin = _mm_sub_ps(mIntrin, _mm_set1_ps(scalar));
    return *this;
}

FX_FORCE_INLINE FxVec3f& FxVec3f::operator*=(const FxVec3f& other)
{
    mIntrin = _mm_mul_ps(mIntrin, other);
    return *this;
}



#endif // #ifdef FX_USE_AVX