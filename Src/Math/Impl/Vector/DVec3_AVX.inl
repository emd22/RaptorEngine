#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_AVX
#include <Math/DVec3.hpp>
#include <Math/SIMDHelper.hpp>
#include <Math/SSE.hpp>

namespace fx {

/////////////////////////////////////
// Constructors
/////////////////////////////////////

FX_FORCE_INLINE DVec3::DVec3(const double* values)
{
	// Only read three values to avoid overstepping the buffer, and zero the W component
	mIntrin = simd::LoadDouble4(values[0], values[1], values[2], 0.0);
}

FX_FORCE_INLINE DVec3::DVec3(const FVec3 other) { mIntrin = _mm256_cvtps_pd(other.mIntrin); }

/////////////////////////////////////
// Operator overloads
/////////////////////////////////////

FX_FORCE_INLINE DVec3 DVec3::operator-() const { return _mm256_xor_pd(mIntrin, _mm256_set1_pd(-0.0)); }

FX_FORCE_INLINE DVec3 DVec3::operator+(const DVec3& other) const
{
	return DVec3(_mm256_add_pd(mIntrin, other.mIntrin));
}

FX_FORCE_INLINE DVec3 DVec3::operator-(const DVec3& other) const
{
	return DVec3(_mm256_sub_pd(mIntrin, other.mIntrin));
}

FX_FORCE_INLINE DVec3 DVec3::operator*(const DVec3& other) const
{
	return DVec3(_mm256_mul_pd(mIntrin, other.mIntrin));
}

FX_FORCE_INLINE DVec3 DVec3::operator*(const double scalar) const
{
	return DVec3(_mm256_mul_pd(mIntrin, _mm256_set1_pd(scalar)));
}

FX_FORCE_INLINE DVec3 DVec3::operator/(const DVec3& other) const
{
	return DVec3(_mm256_div_pd(mIntrin, other.mIntrin));
}

FX_FORCE_INLINE bool DVec3::operator>(const DVec3& other) const
{
	return (_mm256_movemask_pd(_mm256_cmp_pd(mIntrin, other.mIntrin, _CMP_GT_OQ)) & 0b0111) == 0b0111;
}

FX_FORCE_INLINE bool DVec3::operator<(const DVec3& other) const
{
	return (_mm256_movemask_pd(_mm256_cmp_pd(mIntrin, other.mIntrin, _CMP_LT_OQ)) & 0b0111) == 0b0111;
}

FX_FORCE_INLINE bool DVec3::operator>=(const DVec3& other) const
{
	return (_mm256_movemask_pd(_mm256_cmp_pd(mIntrin, other.mIntrin, _CMP_GE_OQ)) & 0b0111) == 0b0111;
}

FX_FORCE_INLINE bool DVec3::operator<=(const DVec3& other) const
{
	return (_mm256_movemask_pd(_mm256_cmp_pd(mIntrin, other.mIntrin, _CMP_LE_OQ)) & 0b0111) == 0b0111;
}

FX_FORCE_INLINE double DVec3::Dot(const DVec3& other) const { return simd::Dot(mIntrin, other.mIntrin); }

FX_FORCE_INLINE DVec3 DVec3::Cross(const DVec3& other) const
{
	using namespace simd;

	const DOUBLE4 a_yzxw = Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(mIntrin);
	const DOUBLE4 b_yzxw = Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(other.mIntrin);

	const DOUBLE4 result_yzxw = _mm256_sub_pd(_mm256_mul_pd(mIntrin, b_yzxw), _mm256_mul_pd(a_yzxw, other.mIntrin));

	return DVec3(Permute4d<Shuffle_AY, Shuffle_AZ, Shuffle_AX, Shuffle_AW>(result_yzxw));
}

FX_FORCE_INLINE FVec3 DVec3::ToFVec3() const { return FVec3(_mm256_cvtpd_ps(mIntrin)); }

} // namespace fx

#endif
