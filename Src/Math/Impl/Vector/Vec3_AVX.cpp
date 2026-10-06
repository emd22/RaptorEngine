#include <Core/Defines.hpp>

#ifdef FX_USE_AVX


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

    // v' = q * v * conj(q), with v as the pure quaternion (x, y, z, 0)
    const Quat vec = Quat(_mm_blend_ps(mIntrin, zero_v, 0b1000));
    const Quat result = rotation * vec * rotation.Conjugate();

    // W is zero in exact arithmetic; clear any rounding residue so the Vec3f's W lane stays 0
    return Vec3f(_mm_blend_ps(result.mIntrin, zero_v, 0b1000));
}


Vec3f Vec3f::Cross(const Vec3f& other) const { return Vec3f(SSE::Cross(mIntrin, other.mIntrin)); }




} // namespace fx

#endif
