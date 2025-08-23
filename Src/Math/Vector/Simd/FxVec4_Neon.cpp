#ifdef FX_USE_NEON

#include <Math/FxVec4.hpp>
#include <arm_neon.h>

const FxVec4f FxVec4f::Zero = FxVec4f(0.0f, 0.0f, 0.0f, 0.0f);
const FxVec4f FxVec4f::One  = FxVec4f(1.0f, 1.0f, 1.0f, 1.0f);


///////////////////////////////
// Value Constructors
///////////////////////////////

FxVec4f::FxVec4f(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = {x, y, z, w};
    this->mIntrin = vld1q_f32(values);
}


FxVec4f::FxVec4f(float32 values[4])
{
    this->mIntrin = vld1q_f32(values);
}


FxVec4f::FxVec4f(float32 scalar)
{
    mIntrin = vdupq_n_f32(scalar);
}


void FxVec4f::Set(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = {x, y, z, w};
    this->mIntrin = vld1q_f32(values);
}



///////////////////////////////
// Vec + Vec Operators
///////////////////////////////

FxVec4f FxVec4f::operator + (const FxVec4f& other) const
{
    return FxVec4f(vaddq_f32(mIntrin, other.mIntrin));
}

FxVec4f FxVec4f::operator - (const FxVec4f& other) const
{
    return FxVec4f(vsubq_f32(mIntrin, other.mIntrin));
}

FxVec4f FxVec4f::operator * (const FxVec4f& other) const
{
    return FxVec4f(vmulq_f32(mIntrin, other.mIntrin));
}

FxVec4f FxVec4f::operator / (const FxVec4f& other) const
{
    return FxVec4f(vdivq_f32(mIntrin, other.mIntrin));
}


///////////////////////////////
// Vec + Scalar Operators
///////////////////////////////

FxVec4f FxVec4f::operator * (float scalar) const
{
    const float32x4_t rvalue = vdupq_n_f32(scalar);
    return FxVec4f(vmulq_f32(mIntrin, rvalue));
}

FxVec4f FxVec4f::operator / (float scalar) const
{
    const float32x4_t rvalue = vdupq_n_f32(scalar);
    return FxVec4f(vdivq_f32(mIntrin, rvalue));
}

FxVec4f FxVec4f::operator - () const
{
    return FxVec4f(vnegq_f32(mIntrin));
}


FxVec4f& FxVec4f::operator += (const FxVec4f& other)
{
    this->mIntrin = vaddq_f32(this->mIntrin, other);
    return *this;
}

FxVec4f& FxVec4f::operator -= (const FxVec4f& other)
{
    this->mIntrin = vsubq_f32(this->mIntrin, other);
    return *this;
}

FxVec4f& FxVec4f::operator *= (const FxVec4f& other)
{
    this->mIntrin = vmulq_f32(this->mIntrin, other);
    return *this;
}

FxVec4f& FxVec4f::operator = (const FxVec4f& other)
{
    this->mIntrin = other.mIntrin;
    return *this;
}



void FxVec4f::Load4Ptr(float32 *values)
{
    this->mIntrin = vld1q_f32(values);
}

void FxVec4f::Load1(float32 value)
{
    this->mIntrin = vdupq_n_f32(value);
}

void FxVec4f::Load4(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = {x, y, z, w};
    this->mIntrin = vld1q_f32(values);
}


#endif // #ifdef FX_USE_NEON
