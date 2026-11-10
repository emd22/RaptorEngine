/*
 * File:        DVec3.hpp
 * Author:      emd22
 * Created:     23/09/2026
 * Description: Vector3 with double precision
 */

#pragma once

#include "FVec3.hpp"
#include "SIMDHelper.hpp"

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

class DVec3
{
public:
	// Constants are defined in source files for each platform.

	/// DVec3 with all components of 0.0.
	static const DVec3 scZero;
	/// DVec3 with all components of 1.0.
	static const DVec3 scOne;

	static const DVec3 scUp;
	static const DVec3 scRight;
	static const DVec3 scForward;


public:
	DVec3() = default;
	DVec3(DOUBLE4 vec) : mIntrin(vec) {}

	FX_FORCE_INLINE DVec3(const double* values);
	FX_FORCE_INLINE DVec3(double x, double y, double z, double w) { mIntrin = simd::LoadDouble4(x, y, z, w); }
	FX_FORCE_INLINE DVec3(double x, double y, double z) { mIntrin = simd::LoadDouble4(x, y, z, 0.0); }

	FX_FORCE_INLINE explicit DVec3(const double scalar) { mIntrin = simd::LoadDouble4(scalar); }
	FX_FORCE_INLINE explicit DVec3(const FVec3 other);

	FX_FORCE_INLINE DVec3 operator-() const;

	FX_FORCE_INLINE DVec3 operator+(const DVec3& other) const;
	FX_FORCE_INLINE DVec3 operator-(const DVec3& other) const;
	FX_FORCE_INLINE DVec3 operator*(const DVec3& other) const;
	FX_FORCE_INLINE DVec3 operator*(const double scalar) const;

	FX_FORCE_INLINE DVec3 operator/(const DVec3& other) const;

	FX_FORCE_INLINE bool operator>(const DVec3& other) const;
	FX_FORCE_INLINE bool operator<(const DVec3& other) const;
	FX_FORCE_INLINE bool operator>=(const DVec3& other) const;
	FX_FORCE_INLINE bool operator<=(const DVec3& other) const;

	FX_FORCE_INLINE FVec3 ToFVec3() const;

	FX_FORCE_INLINE double Dot(const DVec3& other) const;
	FX_FORCE_INLINE double Length() const { return std::sqrt(Dot(*this)); }
	FX_FORCE_INLINE DVec3 Normalized() const { return (*this) * (1.0 / Length()); }

	FX_FORCE_INLINE DVec3 Cross(const DVec3& other) const;

	DVec3& operator=(const DOUBLE4 other)
	{
		mIntrin = other;
		return *this;
	}

public:
	union alignas(alignof(DOUBLE4))
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

RS_ARRAY_TYPE(fx::DVec3, double, 3)


#ifdef FX_USE_AVX
#include "Impl/Vector/DVec3_AVX.inl"
#else
#include "Impl/Vector/DVec3_Neon.inl"
#endif
