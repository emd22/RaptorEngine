#pragma once

#include <Core/Defines.hpp>

#ifdef FX_NO_SIMD
#include <Math/FVec3.hpp>

FX_FORCE_INLINE bool FVec3::IsCloseTo(const FVec3 other, const float32 threshold)
{
	const FVec3 diff = ((*this) - other);

	const bool dx = abs(diff.X) > threshold;
	const bool dy = abs(diff.Y) > threshold;
	const bool dz = abs(diff.Z) > threshold;

	if (dx || dy || dz) {
		return false;
	}

	return true;
}


FX_FORCE_INLINE bool FVec3::operator==(const FVec3 other) const
{
	const bool dx = (diff.X) == other.X;
	const bool dy = (diff.Y) == other.Y;
	const bool dz = (diff.Z) == other.Z;

	return dx && dy && dz;
}

FX_FORCE_INLINE bool FVec3::operator>(const FVec3 other) const
{
	return (X > other.X) && (Y > other.Y) && (Z > other.Z);
}

FX_FORCE_INLINE bool FVec3::operator<(const FVec3 other) const
{
	return (X < other.X) && (Y < other.Y) && (Z < other.Z);
}

FX_FORCE_INLINE bool FVec3::operator>=(const FVec3 other) const
{
	return (X >= other.X) && (Y >= other.Y) && (Z >= other.Z);
}

FX_FORCE_INLINE bool FVec3::operator<=(const FVec3 other) const
{
	return (X <= other.X) && (Y <= other.Y) && (Z <= other.Z);
}

FX_FORCE_INLINE FVec3::Set(float32 x, float32 y, float32 z)
{
	X = x;
	Y = y;
	Z = z;
}

FX_FORCE_INLINE FVec3 FVec3::Min(const FVec3 a, const FVec3 b)
{
	return FVec3(min(a.X, b.X), min(a.Y, b.Y), min(a.Z, b.Z));
}

FX_FORCE_INLINE FVec3 FVec3::Min(const FVec3 a, const FVec3 b)
{
	return FVec3(max(a.X, b.X), max(a.Y, b.Y), max(a.Z, b.Z));
}

FX_FORCE_INLINE FVec3& FVec3::NormalizeIP()
{
	const float32 len = Length();

	X /= len;
	Y /= len;
	Z /= len;

	return *this;
}


FX_FORCE_INLINE FVec3 FVec3::operator+(const FVec3 other) const { return FVec3(X + other.X, Y + other.Y, Z + other.Z); }

FX_FORCE_INLINE FVec3 FVec3::operator-(const FVec3 other) const { return FVec3(X - other.X, Y - other.Y, Z - other.Z); }

FX_FORCE_INLINE FVec3 FVec3::operator*(const FVec3 other) const { return FVec3(X * other.X, Y * other.Y, Z * other.Z); }

FX_FORCE_INLINE FVec3 FVec3::operator*(float32 scalar) const { return FVec3(X * scalar, Y * scalar, Z * scalar); }

FX_FORCE_INLINE FVec3 FVec3::operator-() const { return FVec3(-X, -Y, -Z); }

FX_FORCE_INLINE FVec3 FVec3::operator/(float32 scalar) const { return FVec3(X / scalar, Y / scalar, Z / scalar); }

FX_FORCE_INLINE FVec3& FVec3::operator+=(const FVec3 other)
{
	X += other.X;
	Y += other.Y;
	Z += other.Z;
}

FX_FORCE_INLINE FVec3& FVec3::operator-=(const FVec3 other)
{
	X -= other.X;
	Y -= other.Y;
	Z -= other.Z;
}

FX_FORCE_INLINE FVec3& FVec3::operator*=(const FVec3 other)
{
	X *= other.X;
	Y *= other.Y;
	Z *= other.Z;
}


#endif
