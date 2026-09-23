/*
 * File:        Vec3d.hpp
 * Author:      emd22
 * Created:     23/09/2026
 * Description: Vector3 with double precision
 */

#pragma once

#include "SIMDHelper.hpp"
#include "Vec3.hpp"

#include <Core/Defines.hpp>
#include <Core/Types.hpp>
#include <cmath>
#include <format>

#ifdef FX_USE_NEON
#include "NeonUtil.hpp"

#include <arm_neon.h>
#elif defined(FX_USE_AVX)
#include "SSEUtil.hpp"
#endif

namespace fx {

class Vec3d
{
public:
	Vec3d() = default;
	Vec3d(DOUBLE4 vec) : mIntrin(vec) {}

	FX_FORCE_INLINE Vec3d(const double* values);
	FX_FORCE_INLINE Vec3d(double x, double y, double z, double w) { mIntrin = simd::LoadDouble4(x, y, z, w); }
	FX_FORCE_INLINE Vec3d(double x, double y, double z) { mIntrin = simd::LoadDouble4(x, y, z, 0.0); }

	FX_FORCE_INLINE explicit Vec3d(const double scalar) { mIntrin = simd::LoadDouble4(scalar); }
	FX_FORCE_INLINE explicit Vec3d(const Vec3f& other);

	FX_FORCE_INLINE Vec3d operator+(const Vec3d& other) const;
	FX_FORCE_INLINE Vec3d operator-(const Vec3d& other) const;
	FX_FORCE_INLINE Vec3d operator*(const Vec3d& other) const;
	FX_FORCE_INLINE Vec3d operator*(const double scalar) const;

	FX_FORCE_INLINE Vec3d operator/(const Vec3d& other) const;

	FX_FORCE_INLINE Vec3f ToVec3f() const;

	FX_FORCE_INLINE double Dot(const Vec3d& other) const;
	FX_FORCE_INLINE double Length() const { return std::sqrt(Dot(*this)); }
	FX_FORCE_INLINE Vec3d Normalized() const { return (*this) * (1.0 / Length()); }

	FX_FORCE_INLINE Vec3d Cross(const Vec3d& other) const;

	Vec3d& operator=(const DOUBLE4 other)
	{
		mIntrin = other;
		return *this;
	}

public:
	union alignas(16)
	{
		DOUBLE4 mIntrin;
		double mData[4];
		struct
		{
			double X, Y, Z, W;
		};
	};
};


} // namespace fx


#ifdef FX_USE_AVX
#include "Impl/Vector/Vec3d_AVX.inl"
#else
#include "Impl/Vector/Vec3d_Neon.inl"
#endif
