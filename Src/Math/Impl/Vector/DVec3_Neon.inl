#pragma once

#include <arm_neon.h>

#include <Math/DVec3.hpp>
#include <Math/SIMDHelper.hpp>

namespace fx {

/////////////////////////////////////
// Constructors
/////////////////////////////////////

FX_FORCE_INLINE DVec3::DVec3(const double* values)
{
	mIntrin = simd::LoadDouble4(values);

	// Zero the W component
	mIntrin.val[1] = vsetq_lane_f64(0.0, mIntrin.val[1], 1);
}

FX_FORCE_INLINE DVec3::DVec3(const FVec3 other)
{
	// Widen the low (XY) and high (ZW) halves of the float vector
	mIntrin.val[0] = vcvt_f64_f32(vget_low_f32(other.mIntrin));
	mIntrin.val[1] = vcvt_high_f64_f32(other.mIntrin);
}

/////////////////////////////////////
// Operator overloads
/////////////////////////////////////

FX_FORCE_INLINE DVec3 DVec3::operator-() const
{
	return DOUBLE4 { { vnegq_f64(mIntrin.val[0]), vnegq_f64(mIntrin.val[1]) } };
}

FX_FORCE_INLINE DVec3 DVec3::operator+(const DVec3& other) const
{
	DOUBLE4 result;

	result.val[0] = vaddq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	result.val[1] = vaddq_f64(mIntrin.val[1], other.mIntrin.val[1]);

	return DVec3(result);
}

FX_FORCE_INLINE DVec3 DVec3::operator-(const DVec3& other) const
{
	DOUBLE4 result;

	result.val[0] = vsubq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	result.val[1] = vsubq_f64(mIntrin.val[1], other.mIntrin.val[1]);

	return DVec3(result);
}


FX_FORCE_INLINE DVec3 DVec3::operator*(const DVec3& other) const
{
	DOUBLE4 result = { { vmulq_f64(mIntrin.val[0], other.mIntrin.val[0]),
						 vmulq_f64(mIntrin.val[1], other.mIntrin.val[1]) } };
	return DVec3(result);
}

FX_FORCE_INLINE DVec3 DVec3::operator*(const double scalar) const
{
	DOUBLE4 result = { { vmulq_n_f64(mIntrin.val[0], scalar), vmulq_n_f64(mIntrin.val[1], scalar) } };
	return DVec3(result);
}

FX_FORCE_INLINE DVec3 DVec3::operator/(const DVec3& other) const
{
	DOUBLE4 result;

	result.val[0] = vdivq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	result.val[1] = vdivq_f64(mIntrin.val[1], other.mIntrin.val[1]);

	return DVec3(result);
}

FX_FORCE_INLINE bool DVec3::operator>(const DVec3& other) const
{
	const uint64x2_t xy = vcgtq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	const uint64x2_t zw = vsetq_lane_u64(UINT64_MAX, vcgtq_f64(mIntrin.val[1], other.mIntrin.val[1]), 1);
	return vminv_u32(vmovn_u64(vandq_u64(xy, zw))) == UINT32_MAX;
}

FX_FORCE_INLINE bool DVec3::operator<(const DVec3& other) const
{
	const uint64x2_t xy = vcltq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	const uint64x2_t zw = vsetq_lane_u64(UINT64_MAX, vcltq_f64(mIntrin.val[1], other.mIntrin.val[1]), 1);
	return vminv_u32(vmovn_u64(vandq_u64(xy, zw))) == UINT32_MAX;
}

FX_FORCE_INLINE bool DVec3::operator>=(const DVec3& other) const
{
	const uint64x2_t xy = vcgeq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	const uint64x2_t zw = vsetq_lane_u64(UINT64_MAX, vcgeq_f64(mIntrin.val[1], other.mIntrin.val[1]), 1);
	return vminv_u32(vmovn_u64(vandq_u64(xy, zw))) == UINT32_MAX;
}

FX_FORCE_INLINE bool DVec3::operator<=(const DVec3& other) const
{
	const uint64x2_t xy = vcleq_f64(mIntrin.val[0], other.mIntrin.val[0]);
	const uint64x2_t zw = vsetq_lane_u64(UINT64_MAX, vcleq_f64(mIntrin.val[1], other.mIntrin.val[1]), 1);
	return vminv_u32(vmovn_u64(vandq_u64(xy, zw))) == UINT32_MAX;
}


FX_FORCE_INLINE double DVec3::Dot(const DVec3& other) const { return simd::Dot(mIntrin, other.mIntrin); }

FX_FORCE_INLINE DVec3 DVec3::Cross(const DVec3& other) const
{
	using namespace simd;

	const DOUBLE4 a_yzxw = Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(mIntrin);
	const DOUBLE4 b_yzxw = Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(other.mIntrin);

	DOUBLE4 result_yzxw;
	result_yzxw.val[0] = vsubq_f64(vmulq_f64(mIntrin.val[0], b_yzxw.val[0]),
								   vmulq_f64(a_yzxw.val[0], other.mIntrin.val[0]));
	result_yzxw.val[1] = vsubq_f64(vmulq_f64(mIntrin.val[1], b_yzxw.val[1]),
								   vmulq_f64(a_yzxw.val[1], other.mIntrin.val[1]));

	return DVec3(Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(result_yzxw));
}


FX_FORCE_INLINE FVec3 DVec3::ToFVec3() const
{
	float32x4_t result_v = vcombine_f32(vcvt_f32_f64(mIntrin.val[0]), vcvt_f32_f64(mIntrin.val[1]));
	return FVec3(result_v);
}


} // namespace fx
