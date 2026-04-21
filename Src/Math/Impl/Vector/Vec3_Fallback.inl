#pragma once

#include <Core/Defines.hpp>

#ifdef FX_NO_SIMD
#include <Math/Vec3.hpp>

FX_FORCE_INLINE bool Vec3f::IsCloseTo(const Vec3f& other, const float32 threshold)
{
    const Vec3f diff = ((*this) - other);

    const bool dx = abs(diff.X) > threshold;
    const bool dy = abs(diff.Y) > threshold;
    const bool dz = abs(diff.Z) > threshold;

    if (dx || dy || dz) {
        return false;
    }

    return true;
}


FX_FORCE_INLINE bool Vec3f::operator==(const Vec3f& other) const
{
    const bool dx = (diff.X) == other.X;
    const bool dy = (diff.Y) == other.Y;
    const bool dz = (diff.Z) == other.Z;

    return dx && dy && dz;
}

FX_FORCE_INLINE Vec3f::Set(float32 x, float32 y, float32 z)
{
    X = x;
    Y = y;
    Z = z;
}

FX_FORCE_INLINE Vec3f Vec3f::Min(const Vec3f& a, const Vec3f& b)
{
    return Vec3f(min(a.X, b.X), min(a.Y, b.Y), min(a.Z, b.Z));
}

FX_FORCE_INLINE Vec3f Vec3f::Min(const Vec3f& a, const Vec3f& b)
{
    return Vec3f(max(a.X, b.X), max(a.Y, b.Y), max(a.Z, b.Z));
}

FX_FORCE_INLINE Vec3f& Vec3f::NormalizeIP()
{
    const float32 len = Length();

    X /= len;
    Y /= len;
    Z /= len;

    return *this;
}


FX_FORCE_INLINE Vec3f Vec3f::operator+(const Vec3f& other) const
{
    return Vec3f(X + other.X, Y + other.Y, Z + other.Z);
}

FX_FORCE_INLINE Vec3f Vec3f::operator-(const Vec3f& other) const
{
    return Vec3f(X - other.X, Y - other.Y, Z - other.Z);
}

FX_FORCE_INLINE Vec3f Vec3f::operator*(const Vec3f& other) const
{
    return Vec3f(X * other.X, Y * other.Y, Z * other.Z);
}

FX_FORCE_INLINE Vec3f Vec3f::operator*(float32 scalar) const { return Vec3f(X * scalar, Y * scalar, Z * scalar); }

FX_FORCE_INLINE Vec3f Vec3f::operator-() const { return Vec3f(-X, -Y, -Z); }

FX_FORCE_INLINE Vec3f Vec3f::operator/(float32 scalar) const { return Vec3f(X / scalar, Y / scalar, Z / scalar); }

FX_FORCE_INLINE Vec3f& Vec3f::operator+=(const Vec3f& other)
{
    X += other.X;
    Y += other.Y;
    Z += other.Z;
}

FX_FORCE_INLINE Vec3f& Vec3f::operator-=(const Vec3f& other)
{
    X -= other.X;
    Y -= other.Y;
    Z -= other.Z;
}

FX_FORCE_INLINE Vec3f& Vec3f::operator*=(const Vec3f& other)
{
    X *= other.X;
    Y *= other.Y;
    Z *= other.Z;
}


#endif
