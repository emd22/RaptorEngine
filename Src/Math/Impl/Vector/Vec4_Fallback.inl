#pragma once

#include <Math/Vec4.hpp>

#ifdef FX_NO_SIMD

namespace fx {

FX_FORCE_INLINE float32 Vec4f::LengthSquared() const { return ((X * X) + (Y * Y) + (Z * Z) + (W * W)); }

FX_FORCE_INLINE bool Vec4f::operator>(const Vec4f other) const
{
	return (X > other.X) && (Y > other.Y) && (Z > other.Z) && (W > other.W);
}

FX_FORCE_INLINE bool Vec4f::operator<(const Vec4f other) const
{
	return (X < other.X) && (Y < other.Y) && (Z < other.Z) && (W < other.W);
}

FX_FORCE_INLINE bool Vec4f::operator>=(const Vec4f other) const
{
	return (X >= other.X) && (Y >= other.Y) && (Z >= other.Z) && (W >= other.W);
}

FX_FORCE_INLINE bool Vec4f::operator<=(const Vec4f other) const
{
	return (X <= other.X) && (Y <= other.Y) && (Z <= other.Z) && (W <= other.W);
}

} // namespace fx

#endif
