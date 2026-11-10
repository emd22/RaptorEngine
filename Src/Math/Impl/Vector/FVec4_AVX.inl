#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_AVX
#include <Math/FVec4.hpp>
#include <Math/SSE.hpp>
#include <Math/SSEUtil.hpp>

namespace fx {

FX_FORCE_INLINE FVec4::FVec4(float32 x, float32 y, float32 z, float32 w)
{
	const float32 values[4] = { x, y, z, w };
	mIntrin = _mm_loadu_ps(values);
}

FX_FORCE_INLINE FVec4::FVec4(const float32* values) { mIntrin = _mm_loadu_ps(values); }
FX_FORCE_INLINE FVec4::FVec4(float32 scalar) { mIntrin = _mm_set1_ps(scalar); }


FX_FORCE_INLINE float32 FVec4::LengthSquared() const { return SSE::LengthSquared(mIntrin); }

FX_FORCE_INLINE void FVec4::Set(float32 x, float32 y, float32 z, float32 w) { mIntrin = _mm_setr_ps(x, y, z, w); }
FX_FORCE_INLINE void FVec4::Set(float32 scalar) { mIntrin = _mm_set1_ps(scalar); }

///////////////////////////////
// Vec + Vec Operators
///////////////////////////////

FVec4 FVec4::operator+(const FVec4 other) const { return FVec4(_mm_add_ps(mIntrin, other.mIntrin)); }

FVec4 FVec4::operator-(const FVec4 other) const { return FVec4(_mm_sub_ps(mIntrin, other.mIntrin)); }

FVec4 FVec4::operator*(const FVec4 other) const { return FVec4(_mm_mul_ps(mIntrin, other.mIntrin)); }

FVec4 FVec4::operator/(const FVec4 other) const { return FVec4(_mm_div_ps(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE bool FVec4::operator>(const FVec4 other) const
{
	return _mm_movemask_ps(_mm_cmpgt_ps(mIntrin, other.mIntrin)) == 0b1111;
}

FX_FORCE_INLINE bool FVec4::operator<(const FVec4 other) const
{
	return _mm_movemask_ps(_mm_cmplt_ps(mIntrin, other.mIntrin)) == 0b1111;
}

FX_FORCE_INLINE bool FVec4::operator>=(const FVec4 other) const
{
	return _mm_movemask_ps(_mm_cmpge_ps(mIntrin, other.mIntrin)) == 0b1111;
}

FX_FORCE_INLINE bool FVec4::operator<=(const FVec4 other) const
{
	return _mm_movemask_ps(_mm_cmple_ps(mIntrin, other.mIntrin)) == 0b1111;
}

///////////////////////////////
// Vec + Scalar Operators
///////////////////////////////

FX_FORCE_INLINE FVec4 FVec4::Normalize() const { return FVec4(SSE::Normalize(mIntrin)); }
FX_FORCE_INLINE FVec4& FVec4::NormalizeIP()
{
	mIntrin = (SSE::Normalize(mIntrin));
	return *this;
}

FX_FORCE_INLINE FVec4 FVec4::operator*(float32 scalar) const
{
	const __m128 scalar_v = _mm_set1_ps(scalar);
	return FVec4(_mm_mul_ps(mIntrin, scalar_v));
}

FX_FORCE_INLINE FVec4 FVec4::operator/(float32 scalar) const
{
	const __m128 scalar_v = _mm_set1_ps(scalar);
	return FVec4(_mm_div_ps(mIntrin, scalar_v));
}

FX_FORCE_INLINE FVec4 FVec4::operator-() const { return FVec4(_mm_xor_ps(mIntrin, _mm_set1_ps(-0.0f))); }


FX_FORCE_INLINE FVec4& FVec4::operator+=(const FVec4 other)
{
	mIntrin = _mm_add_ps(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec4& FVec4::operator-=(const FVec4 other)
{
	mIntrin = _mm_sub_ps(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec4& FVec4::operator*=(const FVec4 other)
{
	mIntrin = _mm_mul_ps(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec4& FVec4::operator*=(float32 scalar)
{
	const __m128 scalar_v = _mm_set1_ps(scalar);
	mIntrin = _mm_mul_ps(mIntrin, scalar_v);
	return *this;
}

FX_FORCE_INLINE FVec4& FVec4::operator=(const FVec4 other)
{
	mIntrin = other.mIntrin;
	return *this;
}

} // namespace fx

#endif // #ifdef FX_USE_AVX
