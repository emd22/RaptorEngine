#ifdef FX_USE_NEON

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Real.h>
#include <arm_neon.h>

#include <Math/FxNeonUtil.hpp>
#include <Math/FxVec3.hpp>

const FxVec3f FxVec3f::sZero = FxVec3f(0.0f, 0.0f, 0.0f);
const FxVec3f FxVec3f::sOne = FxVec3f(1.0f, 1.0f, 1.0f);

const FxVec3f FxVec3f::sUp = FxVec3f(0.0f, 1.0f, 0.0f);
const FxVec3f FxVec3f::sRight = FxVec3f(1.0f, 0.0f, 0.0f);
const FxVec3f FxVec3f::sForward = FxVec3f(0.0f, 0.0f, 1.0f);

FxVec3f::FxVec3f(float32 x, float32 y, float32 z)
{
    const float32 values[4] = { x, y, z, 0 };
    mIntrin = vld1q_f32(values);
}

FxVec3f::FxVec3f(const float32* values)
{
    const float32 values4[4] = { values[0], values[1], values[2], 0 };
    mIntrin = vld1q_f32(values4);
}

FxVec3f::FxVec3f(float32 scalar) { mIntrin = vdupq_n_f32(scalar); }

FxVec3f::FxVec3f(const JPH::Vec3& other) { FromJoltVec3(other); }


void FxVec3f::Print() const { FxLogInfo("Vec3f {{ X={:.6f}, Y={:.6f}, Z={:.6f} }}", X, Y, Z); }

float32 FxVec3f::Length() const
{
    float32x4_t vec = mIntrin;

    // Square the vector
    vec = vmulq_f32(vec, vec);
    // Add all components (horizontal add)
    float32 len2 = vaddvq_f32(vec);

    return sqrtf(len2);
}

FxVec3f FxVec3f::Normalize() const
{
    // Calculate length
    const float32 len = Length();

    // Splat to register
    const float32x4_t len_v = vdupq_n_f32(len);

    // Load vector into register
    float32x4_t v = mIntrin;
    // Divide vector by length
    v = vdivq_f32(v, len_v);

    return FxVec3f(v);
}

FxVec3f FxVec3f::CrossSlow(const FxVec3f& other) const
{
    const float32 ax = mData[0];
    const float32 bx = other.mData[0];

    const float32 ay = mData[1];
    const float32 by = other.mData[1];

    const float32 az = mData[2];
    const float32 bz = other.mData[2];

    return FxVec3f(ay * bz - by * az, az * bx - bz * ax, ax * by - bx * ay);
}

FxVec3f FxVec3f::Cross(const FxVec3f& other) const
{
    const float32x4_t a = mIntrin;
    const float32x4_t b = other.mIntrin;

    float32x4_t a_yzxw = FxNeon::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(a);
    float32x4_t b_yzxw = FxNeon::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(b);

    const float32x4_t result_yzxw = vsubq_f32(vmulq_f32(a, b_yzxw), vmulq_f32(a_yzxw, b));

    return FxVec3f(FxNeon::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(result_yzxw));
}

float32 FxVec3f::Dot(const FxVec3f& other) const
{
    float32x4_t prod = vmulq_f32(mIntrin, other.mIntrin);
    return vaddvq_f32(prod);
}

void FxVec3f::ToJoltVec3(JPH::RVec3& jolt_vec) const { jolt_vec.mValue = mIntrin; }
void FxVec3f::FromJoltVec3(const JPH::RVec3& jolt_vec) { mIntrin = jolt_vec.mValue; }

bool FxVec3f::IsCloseTo(const JPH::Vec3& other, const float32 tolerance) const
{
    return IsCloseTo(other.mValue, tolerance);
}


bool FxVec3f::operator==(const JPH::Vec3& other) const { return (*this) == FxVec3f(other.mValue); }

#endif
