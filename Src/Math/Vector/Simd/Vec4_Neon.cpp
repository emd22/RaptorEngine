#ifdef FX_USE_NEON

#include <Math/Vec4.hpp>
#include <arm_neon.h>

FxVec4f::FxVec4f(float32 values[4])
{
    this->mIntrin = vld1q_f32(values);
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

FxVec4f::FxVec4f(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = {x, y, z, w};
    this->mIntrin = vld1q_f32(values);
}

FxVec4f::FxVec4f(float32 scalar)
{
    this->mIntrin = vdupq_n_f32(scalar);
}

FxVec4f FxVec4f::MulAdd(const FxVec4f &add_value, const FxVec4f &mul_a, const FxVec4f &mul_b)
{
    FxVec4f result;
    result.mIntrin = vmlaq_f32(add_value, mul_a, mul_b);
    return result;
}

FxVec4f FxVec4f::operator + (const FxVec4f &other) const
{
    FxVec4f result = FxVec4f(this->mIntrin);
    result += other;
    return result;
}

FxVec4f FxVec4f::operator * (const FxVec4f &other) const
{
    FxVec4f result = FxVec4f(this->mIntrin);
    result *= other;
    return result;
}

FxVec4f &FxVec4f::operator += (const FxVec4f &other)
{
    this->mIntrin = vaddq_f32(this->mIntrin, other);
    return *this;
}

FxVec4f &FxVec4f::operator -= (const FxVec4f &other)
{
    this->mIntrin = vsubq_f32(this->mIntrin, other);
    return *this;
}

FxVec4f &FxVec4f::operator *= (const FxVec4f &other)
{
    this->mIntrin = vmulq_f32(this->mIntrin, other);
    return *this;
}

FxVec4f &FxVec4f::operator = (const FxVec4f &other)
{
    this->mIntrin = other.mIntrin;
    return *this;
}


/////////////////////////////////
// Component Getters
/////////////////////////////////

float32 FxVec4f::GetX() const
{
    return this->mData[0];
}

float32 FxVec4f::GetY() const
{
    return this->mData[1];
}

float32 FxVec4f::GetZ() const
{
    return this->mData[2];
}

float32 FxVec4f::GetW() const
{
    return this->mData[3];
}

/////////////////////////////////
// Component Setters
/////////////////////////////////

void FxVec4f::SetX(float32 x)
{
    this->mIntrin = vsetq_lane_f32(x, this->mIntrin, 0);
}

void FxVec4f::SetY(float32 y)
{
    this->mIntrin = vsetq_lane_f32(y, this->mIntrin, 1);
}

void FxVec4f::SetZ(float32 z)
{
    this->mIntrin = vsetq_lane_f32(z, this->mIntrin, 2);
}

void FxVec4f::SetW(float32 w)
{
    this->mIntrin = vsetq_lane_f32(w, this->mIntrin, 3);
}

#endif // #ifdef FX_USE_NEON
