#pragma once

#include <arm_neon.h>

#include <Math/SIMDHelper.hpp>
#include <Math/Vec3d.hpp>

namespace fx {

/////////////////////////////////////
// Constructors
/////////////////////////////////////

FX_FORCE_INLINE Vec3d::Vec3d(const double* values)
{
	mIntrin = simd::LoadDouble4(values);

	// Zero the W component
	mIntrin.val[1] = vsetq_lane_f64(0.0, mIntrin.val[1], 1);
}

FX_FORCE_INLINE Vec3d::Vec3d(const Vec3f& other)
{
	// Widen the low (XY) and high (ZW) halves of the float vector
	mIntrin.val[0] = vcvt_f64_f32(vget_low_f32(other.mIntrin));
	mIntrin.val[1] = vcvt_high_f64_f32(other.mIntrin);
}

/////////////////////////////////////
// Operator overloads
/////////////////////////////////////

FX_FORCE_INLINE Vec3d Vec3d::operator+(const Vec3d& other) const
{
	DOUBLE4 result;

	result.val[0] = vaddq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	result.val[1] = vaddq_f64(mIntrin.val[1], other.mIntrin.val[1]);

	return Vec3d(result);
}

FX_FORCE_INLINE Vec3d Vec3d::operator-(const Vec3d& other) const
{
	DOUBLE4 result;

	result.val[0] = vsubq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	result.val[1] = vsubq_f64(mIntrin.val[1], other.mIntrin.val[1]);

	return Vec3d(result);
}


FX_FORCE_INLINE Vec3d Vec3d::operator*(const Vec3d& other) const
{
	DOUBLE4 result = { { vmulq_f64(mIntrin.val[0], other.mIntrin.val[0]),
						 vmulq_f64(mIntrin.val[1], other.mIntrin.val[1]) } };
	return Vec3d(result);
}

FX_FORCE_INLINE Vec3d Vec3d::operator*(const double scalar) const
{
	DOUBLE4 result = { { vmulq_n_f64(mIntrin.val[0], scalar), vmulq_n_f64(mIntrin.val[1], scalar) } };
	return Vec3d(result);
}

FX_FORCE_INLINE Vec3d Vec3d::operator/(const Vec3d& other) const
{
	DOUBLE4 result;

	result.val[0] = vdivq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	result.val[1] = vdivq_f64(mIntrin.val[1], other.mIntrin.val[1]);

	return Vec3d(result);
}


FX_FORCE_INLINE double Vec3d::Dot(const Vec3d& other) const { return simd::Dot(mIntrin, other.mIntrin); }

FX_FORCE_INLINE Vec3d Vec3d::Cross(const Vec3d& other) const
{
	using namespace Neon;

	const DOUBLE4 a_yzxw = Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(mIntrin);
	const DOUBLE4 b_yzxw = Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(other.mIntrin);

	DOUBLE4 result_yzxw;
	result_yzxw.val[0] = vsubq_f64(vmulq_f64(mIntrin.val[0], b_yzxw.val[0]),
								   vmulq_f64(a_yzxw.val[0], other.mIntrin.val[0]));
	result_yzxw.val[1] = vsubq_f64(vmulq_f64(mIntrin.val[1], b_yzxw.val[1]),
								   vmulq_f64(a_yzxw.val[1], other.mIntrin.val[1]));

	return Vec3d(Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(result_yzxw));
}


FX_FORCE_INLINE Vec3f Vec3d::ToVec3f() const
{
	float32x4_t result_v = vcombine_f32(vcvt_f32_f64(mIntrin.val[0]), vcvt_f32_f64(mIntrin.val[1]));
	return Vec3f(result_v);
}


} // namespace fx
