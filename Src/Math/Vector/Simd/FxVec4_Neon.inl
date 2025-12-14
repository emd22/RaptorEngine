#pragma once

#include <Math/FxVec4.hpp>

#ifdef FX_USE_NEON

FX_FORCE_INLINE float32 FxVec4f::LengthSquared() const
{
    // Preload into register
    float32x4_t vec = mIntrin;

    vec = vmulq_f32(vec, vec);

    // Calculate the length squared by doing a horizontal add (add all components)
    return vaddvq_f32(vec);
}


///////////////////////////////
// Vec + Vec Operators
///////////////////////////////

FxVec4f FxVec4f::operator+(const FxVec4f& other) const { return FxVec4f(vaddq_f32(mIntrin, other.mIntrin)); }

FxVec4f FxVec4f::operator-(const FxVec4f& other) const { return FxVec4f(vsubq_f32(mIntrin, other.mIntrin)); }

FxVec4f FxVec4f::operator*(const FxVec4f& other) const { return FxVec4f(vmulq_f32(mIntrin, other.mIntrin)); }

FxVec4f FxVec4f::operator/(const FxVec4f& other) const { return FxVec4f(vdivq_f32(mIntrin, other.mIntrin)); }


///////////////////////////////
// Vec + Scalar Operators
///////////////////////////////

FX_FORCE_INLINE FxVec4f FxVec4f::operator*(float scalar) const { return FxVec4f(vmulq_n_f32(mIntrin, scalar)); }
FX_FORCE_INLINE FxVec4f& FxVec4f::operator*=(float scalar)
{
    mIntrin = vmulq_n_f32(mIntrin, scalar);
    return *this;
}

FX_FORCE_INLINE FxVec4f FxVec4f::operator/(float scalar) const
{
    const float32x4_t rvalue = vdupq_n_f32(scalar);
    return FxVec4f(vdivq_f32(mIntrin, rvalue));
}

FX_FORCE_INLINE FxVec4f FxVec4f::operator-() const { return FxVec4f(vnegq_f32(mIntrin)); }


FX_FORCE_INLINE FxVec4f& FxVec4f::operator+=(const FxVec4f& other)
{
    this->mIntrin = vaddq_f32(this->mIntrin, other);
    return *this;
}

FX_FORCE_INLINE FxVec4f& FxVec4f::operator-=(const FxVec4f& other)
{
    this->mIntrin = vsubq_f32(this->mIntrin, other);
    return *this;
}

FX_FORCE_INLINE FxVec4f& FxVec4f::operator*=(const FxVec4f& other)
{
    this->mIntrin = vmulq_f32(this->mIntrin, other);
    return *this;
}

FX_FORCE_INLINE FxVec4f& FxVec4f::operator=(const FxVec4f& other)
{
    this->mIntrin = other.mIntrin;
    return *this;
}

#endif
