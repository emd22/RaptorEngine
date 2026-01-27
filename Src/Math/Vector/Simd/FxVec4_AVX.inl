#pragma once

#include <Core/FxDefines.hpp>
#include <Math/FxVec4.hpp>

#ifdef FX_USE_AVX

#include <Math/FxAVXUtil.hpp>
#include <Math/FxSSE.hpp>

FX_FORCE_INLINE float32 FxVec4f::LengthSquared() const { return FxSSE::LengthSquared(mIntrin); }


///////////////////////////////
// Vec + Vec Operators
///////////////////////////////

FxVec4f FxVec4f::operator+(const FxVec4f& other) const { return FxVec4f(_mm_add_ps(mIntrin, other.mIntrin)); }

FxVec4f FxVec4f::operator-(const FxVec4f& other) const { return FxVec4f(_mm_sub_ps(mIntrin, other.mIntrin)); }

FxVec4f FxVec4f::operator*(const FxVec4f& other) const { return FxVec4f(_mm_mul_ps(mIntrin, other.mIntrin)); }

FxVec4f FxVec4f::operator/(const FxVec4f& other) const { return FxVec4f(_mm_div_ps(mIntrin, other.mIntrin)); }

///////////////////////////////
// Vec + Scalar Operators
///////////////////////////////

FX_FORCE_INLINE FxVec4f FxVec4f::operator*(float32 scalar) const
{
    const __m128 scalar_v = _mm_set1_ps(scalar);
    return FxVec4f(_mm_mul_ps(mIntrin, scalar_v));
}

FX_FORCE_INLINE FxVec4f FxVec4f::operator/(float32 scalar) const
{
    const __m128 scalar_v = _mm_set1_ps(scalar);
    return FxVec4f(_mm_div_ps(mIntrin, scalar_v));
}

FX_FORCE_INLINE FxVec4f FxVec4f::operator-() const { return FxVec4f(_mm_xor_ps(mIntrin, _mm_set1_ps(-0.0f))); }


FX_FORCE_INLINE FxVec4f& FxVec4f::operator+=(const FxVec4f& other)
{
    mIntrin = _mm_add_ps(mIntrin, other.mIntrin);
    return *this;
}

FX_FORCE_INLINE FxVec4f& FxVec4f::operator-=(const FxVec4f& other)
{
    mIntrin = _mm_sub_ps(mIntrin, other.mIntrin);
    return *this;
}

FX_FORCE_INLINE FxVec4f& FxVec4f::operator*=(const FxVec4f& other)
{
    mIntrin = _mm_mul_ps(mIntrin, other.mIntrin);
    return *this;
}

FX_FORCE_INLINE FxVec4f& FxVec4f::operator*=(float32 scalar)
{
    const __m128 scalar_v = _mm_set1_ps(scalar);
    mIntrin = _mm_mul_ps(mIntrin, scalar_v);
    return *this;
}

FX_FORCE_INLINE FxVec4f& FxVec4f::operator=(const FxVec4f& other)
{
    mIntrin = other.mIntrin;
    return *this;
}

#endif // #ifdef FX_USE_AVX
