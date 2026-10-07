#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_AVX
#include <Math/SIMDHelper.hpp>
#include <Math/SSE.hpp>
#include <Math/Vec3d.hpp>

namespace fx {

/////////////////////////////////////
// Constructors
/////////////////////////////////////

FX_FORCE_INLINE Vec3d::Vec3d(const double* values)
{
	// Only read three values to avoid overstepping the buffer, and zero the W component
	mIntrin = simd::LoadDouble4(values[0], values[1], values[2], 0.0);
}

FX_FORCE_INLINE Vec3d::Vec3d(const Vec3f other) { mIntrin = _mm256_cvtps_pd(other.mIntrin); }

/////////////////////////////////////
// Operator overloads
/////////////////////////////////////

FX_FORCE_INLINE Vec3d Vec3d::operator+(const Vec3d& other) const
{
	return Vec3d(_mm256_add_pd(mIntrin, other.mIntrin));
}

FX_FORCE_INLINE Vec3d Vec3d::operator-(const Vec3d& other) const
{
	return Vec3d(_mm256_sub_pd(mIntrin, other.mIntrin));
}

FX_FORCE_INLINE Vec3d Vec3d::operator*(const Vec3d& other) const
{
	return Vec3d(_mm256_mul_pd(mIntrin, other.mIntrin));
}

FX_FORCE_INLINE Vec3d Vec3d::operator*(const double scalar) const
{
	return Vec3d(_mm256_mul_pd(mIntrin, _mm256_set1_pd(scalar)));
}

FX_FORCE_INLINE Vec3d Vec3d::operator/(const Vec3d& other) const
{
	return Vec3d(_mm256_div_pd(mIntrin, other.mIntrin));
}

FX_FORCE_INLINE double Vec3d::Dot(const Vec3d& other) const { return simd::Dot(mIntrin, other.mIntrin); }

FX_FORCE_INLINE Vec3d Vec3d::Cross(const Vec3d& other) const
{
	using namespace SSE;

	const DOUBLE4 a_yzxw = Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(mIntrin);
	const DOUBLE4 b_yzxw = Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(other.mIntrin);

	const DOUBLE4 result_yzxw = _mm256_sub_pd(_mm256_mul_pd(mIntrin, b_yzxw), _mm256_mul_pd(a_yzxw, other.mIntrin));

	return Vec3d(Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(result_yzxw));
}

FX_FORCE_INLINE Vec3f Vec3d::ToVec3f() const { return Vec3f(_mm256_cvtpd_ps(mIntrin)); }

} // namespace fx

#endif
