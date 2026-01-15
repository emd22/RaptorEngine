#include <Core/FxDefines.hpp>

#ifdef FX_USE_AVX

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Real.h>

#include <Math/FxSSE.hpp>
#include <Math/FxVec3.hpp>
#include <Math/FxVec4.hpp>

const FxVec3f FxVec3f::sZero = FxVec3f(0.0f, 0.0f, 0.0f);
const FxVec3f FxVec3f::sOne = FxVec3f(1.0f, 1.0f, 1.0f);

const FxVec3f FxVec3f::sUp = FxVec3f(0.0f, 1.0f, 0.0f);
const FxVec3f FxVec3f::sRight = FxVec3f(1.0f, 0.0f, 0.0f);
const FxVec3f FxVec3f::sForward = FxVec3f(0.0f, 0.0f, 1.0f);

FxVec3f::FxVec3f(float32 x, float32 y, float32 z)
{
    const float32 values[4] = { x, y, z, 0 };
    mIntrin = _mm_load_ps(values);
}

FxVec3f::FxVec3f(const float32* values)
{
    // Allocate here to avoid unordered loads into our SSE register and avoid overstepping
    // the buffer in `values`
    const float32 values4[4] = { values[0], values[1], values[2], 0 };
    mIntrin = _mm_load_ps(values4);
}

FxVec3f::FxVec3f(float32 scalar) { mIntrin = _mm_set1_ps(scalar); }

FxVec3f::FxVec3f(const JPH::Vec3& other) { FromJoltVec3(other); }

FxVec3f::FxVec3f(const FxVec4f& other) { mIntrin = other.mIntrin; }


void FxVec3f::Print() const { FxLogInfo("Vec3f {{ X={:.6f}, Y={:.6f}, Z={:.6f} }}", X, Y, Z); }

// float32 FxVec3f::Length() const
// {
//     // float32x4_t vec = mIntrin;

//     // Square the vector
//     // vec = vmulq_f32(vec, vec);
//     // Add all components (horizontal add)
//     // float32 len2 = vaddvq_f32(vec);

//     return sqrtf(Dot(mIntrin));
// }

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
    const __m128 a = mIntrin;
    const __m128 b = other.mIntrin;

    const __m128 a_yzxw = FxSSE::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(a);
    const __m128 b_yzxw = FxSSE::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(b);

    const __m128 result_yzxw = _mm_sub_ps(_mm_mul_ps(a, b_yzxw), _mm_mul_ps(a_yzxw, b));

    return FxVec3f(FxSSE::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(result_yzxw));

    //__m128 a_yzxw = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));

    /*__m128 a_yzxw = FxNeon::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(a);
    __m128 b_yzxw = FxNeon::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(b);

    const float32x4_t result_yzxw = vsubq_f32(vmulq_f32(a, b_yzxw), vmulq_f32(a_yzxw, b));

    return FxVec3f(FxNeon::Permute4<FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX, FxShuffle_AW>(result_yzxw));*/
}

void FxVec3f::ToJoltVec3(JPH::RVec3& jolt_vec) const { jolt_vec.mValue = mIntrin; }
void FxVec3f::FromJoltVec3(const JPH::RVec3& jolt_vec) { mIntrin = jolt_vec.mValue; }

bool FxVec3f::IsCloseTo(const JPH::Vec3& other, const float32 tolerance) const
{
    return IsCloseTo(other.mValue, tolerance);
}


bool FxVec3f::operator==(const JPH::Vec3& other) const { return (*this) == FxVec3f(other.mValue); }


#endif
