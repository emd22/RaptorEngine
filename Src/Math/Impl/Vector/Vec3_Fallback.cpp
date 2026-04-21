#include <Core/Defines.hpp>

#ifdef FX_NO_SIMD

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Real.h>

#include <Math/Vec3.hpp>

namespace fx {

const Vec3f Vec3f::sZero = Vec3f(0.0f, 0.0f, 0.0f);
const Vec3f Vec3f::sOne = Vec3f(1.0f, 1.0f, 1.0f);

const Vec3f Vec3f::sUp = Vec3f(0.0f, 1.0f, 0.0f);
const Vec3f Vec3f::sRight = Vec3f(1.0f, 0.0f, 0.0f);
const Vec3f Vec3f::sForward = Vec3f(0.0f, 0.0f, 1.0f);

Vec3f::Vec3f(const JPH::Vec3& other) { FromJoltVec3(other); }


void Vec3f::Print() const { LogInfo("Vec3f {{ X={:.6f}, Y={:.6f}, Z={:.6f} }}", X, Y, Z); }

float32 Vec3f::Length() const
{
    const float32 len2 = (X * X) + (Y * Y) + (Z * Z);
    return sqrtf(len2);
}

Vec3f Vec3f::Normalize() const
{
    // Calculate length
    const float32 len = Length();

    return Vec3f(X / len, Y / len, Z / len);
}

Vec3f Vec3f::CrossSlow(const Vec3f& other) const { return Cross(other); }

Vec3f Vec3f::Cross(const Vec3f& other) const
{
    const float32 ax = X;
    const float32 bx = other.X;

    const float32 ay = Y;
    const float32 by = other.Y;

    const float32 az = Z;
    const float32 bz = other.Z;

    return Vec3f(ay * bz - by * az, az * bx - bz * ax, ax * by - bx * ay);
}

float32 Vec3f::Dot(const Vec3f& other) const
{
    const float32 result = (X * other.X) + (Y * other.Y) + (Z * other.Z);
    return result;
}

void Vec3f::ToJoltVec3(JPH::RVec3& jolt_vec) const
{
    jolt_vec.SetX(X);
    jolt_vec.SetY(Y);
    jolt_vec.SetZ(Z);
}

void Vec3f::FromJoltVec3(const JPH::RVec3& jolt_vec)
{
    X = jolt_vec.GetX();
    Y = jolt_vec.GetY();
    Z = jolt_vec.GetZ();
}


bool Vec3f::IsCloseTo(const JPH::Vec3& other, const float32 threshold) const
{
    return IsCloseTo(Vec3f(other), threshold);
}


//////////////////////////////
// Operator Overloads
//////////////////////////////


bool Vec3f::operator==(const JPH::Vec3& other) const { return (*this) == Vec3f(other); }

} // namespace fx

#endif
