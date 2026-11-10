#pragma once

#include <Math/FVec4.hpp>

#ifdef FX_NO_SIMD

namespace fx {

FX_FORCE_INLINE float32 FVec4::LengthSquared() const { return ((X * X) + (Y * Y) + (Z * Z) + (W * W)); }

FX_FORCE_INLINE bool FVec4::operator>(const FVec4 other) const
{
	return (X > other.X) && (Y > other.Y) && (Z > other.Z) && (W > other.W);
}

FX_FORCE_INLINE bool FVec4::operator<(const FVec4 other) const
{
	return (X < other.X) && (Y < other.Y) && (Z < other.Z) && (W < other.W);
}

FX_FORCE_INLINE bool FVec4::operator>=(const FVec4 other) const
{
	return (X >= other.X) && (Y >= other.Y) && (Z >= other.Z) && (W >= other.W);
}

FX_FORCE_INLINE bool FVec4::operator<=(const FVec4 other) const
{
	return (X <= other.X) && (Y <= other.Y) && (Z <= other.Z) && (W <= other.W);
}

} // namespace fx

#endif
