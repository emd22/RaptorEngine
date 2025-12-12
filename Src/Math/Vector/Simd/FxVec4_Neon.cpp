#include <Core/FxDefines.hpp>

#ifdef FX_USE_NEON

#include <arm_neon.h>

#include <Math/FxVec4.hpp>

const FxVec4f FxVec4f::sZero = FxVec4f(0.0f, 0.0f, 0.0f, 0.0f);
const FxVec4f FxVec4f::sOne = FxVec4f(1.0f, 1.0f, 1.0f, 1.0f);


///////////////////////////////
// Value Constructors
///////////////////////////////

FxVec4f::FxVec4f(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = { x, y, z, w };
    this->mIntrin = vld1q_f32(values);
}


FxVec4f::FxVec4f(float32 values[4]) { this->mIntrin = vld1q_f32(values); }


FxVec4f::FxVec4f(float32 scalar) { mIntrin = vdupq_n_f32(scalar); }


void FxVec4f::Set(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = { x, y, z, w };
    this->mIntrin = vld1q_f32(values);
}


void FxVec4f::Load4Ptr(float32* values) { this->mIntrin = vld1q_f32(values); }

void FxVec4f::Load1(float32 value) { this->mIntrin = vdupq_n_f32(value); }

void FxVec4f::Load4(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = { x, y, z, w };
    this->mIntrin = vld1q_f32(values);
}


#endif // #ifdef FX_USE_NEON
