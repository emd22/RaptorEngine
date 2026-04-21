#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Real.h>

#include <Core/Log.hpp>
#include <Math/Quat.hpp>
#include <Math/SSE.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>

namespace fx {

const Vec3f Vec3f::sZero = Vec3f(0.0f, 0.0f, 0.0f);
const Vec3f Vec3f::sOne = Vec3f(1.0f, 1.0f, 1.0f);

const Vec3f Vec3f::sUp = Vec3f(0.0f, 1.0f, 0.0f);
const Vec3f Vec3f::sRight = Vec3f(1.0f, 0.0f, 0.0f);
const Vec3f Vec3f::sForward = Vec3f(0.0f, 0.0f, 1.0f);


Vec3f::Vec3f(const JPH::Vec3& other) { FromJoltVec3(other); }
Vec3f::Vec3f(const Vec4f& other) { mIntrin = other.mIntrin; }

void Vec3f::Print() const { LogInfo("Vec3f {{ X={:.6f}, Y={:.6f}, Z={:.6f} }}", X, Y, Z); }


Vec3f Vec3f::CrossSlow(const Vec3f& other) const
{
    const float32 ax = mData[0];
    const float32 bx = other.mData[0];

    const float32 ay = mData[1];
    const float32 by = other.mData[1];

    const float32 az = mData[2];
    const float32 bz = other.mData[2];

    return Vec3f(ay * bz - by * az, az * bx - bz * ax, ax * by - bx * ay);
}

Vec3f Vec3f::Rotate(const Quat& rotation) const
{
    const __m128 zero_v = _mm_setzero_ps();

    Quat vec = Quat(_mm_shuffle_ps(mIntrin, zero_v, _MM_SHUFFLE(0, 1, 2, 4)));
    LogInfo("Vec: {}", vec);
    Quat conj_v = rotation.Conjugate();

    Quat result = conj_v * vec;
    result = result * rotation;

    return Vec3f(result.mIntrin);
}


Vec3f Vec3f::Cross(const Vec3f& other) const { return Vec3f(SSE::Cross(mIntrin, other.mIntrin)); }

void Vec3f::ToJoltVec3(JPH::RVec3& jolt_vec) const { jolt_vec.mValue = mIntrin; }
void Vec3f::FromJoltVec3(const JPH::RVec3& jolt_vec) { mIntrin = jolt_vec.mValue; }

bool Vec3f::IsCloseTo(const JPH::Vec3& other, const float32 tolerance) const
{
    return IsCloseTo(other.mValue, tolerance);
}

bool Vec3f::operator==(const JPH::Vec3& other) const { return (*this) == Vec3f(other.mValue); }

} // namespace fx

#endif
