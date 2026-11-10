#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_NEON
#include <Math/FVec4.hpp>

namespace fx {

FX_FORCE_INLINE FVec4::FVec4(float32 x, float32 y, float32 z, float32 w)
{
	const float32 values[4] = { x, y, z, w };
	mIntrin = vld1q_f32(values);
}


FX_FORCE_INLINE FVec4::FVec4(const float32* values) { mIntrin = vld1q_f32(values); }


FX_FORCE_INLINE FVec4::FVec4(float32 scalar) { mIntrin = vdupq_n_f32(scalar); }


FX_FORCE_INLINE FVec4 FVec4::Normalize() const { return FVec4(Neon::Normalize(mIntrin)); }
FX_FORCE_INLINE FVec4& FVec4::NormalizeIP()
{
	mIntrin = (Neon::Normalize(mIntrin));
	return *this;
}

FX_FORCE_INLINE float32 FVec4::LengthSquared() const
{
	float32x4_t vec = mIntrin;
	vec = vmulq_f32(vec, vec);

	// Calculate the length squared by doing a horizontal add (add all components)
	return vaddvq_f32(vec);
}

FX_FORCE_INLINE bool FVec4::IsCloseTo(const FVec4 other, const float32 tolerance) const
{
	return IsCloseTo(other.mIntrin);
}

FX_FORCE_INLINE bool FVec4::IsCloseTo(const FVec4::SimdType other, const float32 tolerance) const
{
	// Get the absolute difference between the vectors
	const float32x4_t diff = vabdq_f32(mIntrin, other);

	// Is the difference greater than our threshold?
	const uint32x4_t lt = vcgtq_f32(diff, vdupq_n_f32(tolerance));

	// If any components are true, return false.
	return vmaxvq_u32(lt) == 0;
}

FX_FORCE_INLINE bool FVec4::IsZero() const
{
	const uint32x4_t is_zero_v = vceqzq_f32(mIntrin);
	return vmaxvq_u32(is_zero_v) == 0;
}

FX_FORCE_INLINE bool FVec4::IsNearZero(const float32 tolerance) const { return IsCloseTo(sZero, tolerance); }

FX_FORCE_INLINE void FVec4::Set(float32 x, float32 y, float32 z, float32 w)
{
	const float32 v[4] = { x, y, z, w };
	mIntrin = vld1q_f32(v);
}

FX_FORCE_INLINE void FVec4::Set(float32 scalar) { mIntrin = vdupq_n_f32(scalar); }


///////////////////////////////
// Vec + Vec Operators
///////////////////////////////

FVec4 FVec4::operator+(const FVec4 other) const { return FVec4(vaddq_f32(mIntrin, other.mIntrin)); }

FVec4 FVec4::operator-(const FVec4 other) const { return FVec4(vsubq_f32(mIntrin, other.mIntrin)); }

FVec4 FVec4::operator*(const FVec4 other) const { return FVec4(vmulq_f32(mIntrin, other.mIntrin)); }

FVec4 FVec4::operator/(const FVec4 other) const { return FVec4(vdivq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE bool FVec4::operator>(const FVec4 other) const
{
	return vminvq_u32(vcgtq_f32(mIntrin, other.mIntrin)) == UINT32_MAX;
}

FX_FORCE_INLINE bool FVec4::operator<(const FVec4 other) const
{
	return vminvq_u32(vcltq_f32(mIntrin, other.mIntrin)) == UINT32_MAX;
}

FX_FORCE_INLINE bool FVec4::operator>=(const FVec4 other) const
{
	return vminvq_u32(vcgeq_f32(mIntrin, other.mIntrin)) == UINT32_MAX;
}

FX_FORCE_INLINE bool FVec4::operator<=(const FVec4 other) const
{
	return vminvq_u32(vcleq_f32(mIntrin, other.mIntrin)) == UINT32_MAX;
}


///////////////////////////////
// Vec + Scalar Operators
///////////////////////////////

FX_FORCE_INLINE FVec4 FVec4::operator*(float scalar) const { return FVec4(vmulq_n_f32(mIntrin, scalar)); }
FX_FORCE_INLINE FVec4& FVec4::operator*=(float scalar)
{
	mIntrin = vmulq_n_f32(mIntrin, scalar);
	return *this;
}

FX_FORCE_INLINE FVec4 FVec4::operator/(float scalar) const
{
	const float32x4_t rvalue = vdupq_n_f32(scalar);
	return FVec4(vdivq_f32(mIntrin, rvalue));
}

FX_FORCE_INLINE FVec4 FVec4::operator-() const { return FVec4(vnegq_f32(mIntrin)); }


FX_FORCE_INLINE FVec4& FVec4::operator+=(const FVec4 other)
{
	this->mIntrin = vaddq_f32(this->mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec4& FVec4::operator-=(const FVec4 other)
{
	this->mIntrin = vsubq_f32(this->mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec4& FVec4::operator*=(const FVec4 other)
{
	this->mIntrin = vmulq_f32(this->mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec4& FVec4::operator=(const FVec4 other)
{
	this->mIntrin = other.mIntrin;
	return *this;
}


/////////////////////////////////////
// Integer classes
/////////////////////////////////////


FX_FORCE_INLINE Vec4u::Vec4u(uint32 x, uint32 y, uint32 z, uint32 w)
{
	const uint32 values[4] = { x, y, z, w };
	mIntrin = vld1q_u32(values);
}


FX_FORCE_INLINE Vec4u::Vec4u(const uint32* values) { mIntrin = vld1q_u32(values); }


FX_FORCE_INLINE Vec4u::Vec4u(uint32 scalar) { mIntrin = vdupq_n_u32(scalar); }

FX_FORCE_INLINE bool Vec4u::IsZero() const
{
	const uint32x4_t is_zero_v = vceqzq_u32(mIntrin);
	return vmaxvq_u32(is_zero_v) == 0;
}

FX_FORCE_INLINE void Vec4u::Set(uint32 x, uint32 y, uint32 z, uint32 w)
{
	const uint32 v[4] = { x, y, z, w };
	mIntrin = vld1q_u32(v);
}

FX_FORCE_INLINE void Vec4u::Set(uint32 scalar) { mIntrin = vdupq_n_u32(scalar); }


///////////////////////////////
// Vec + Vec Operators
///////////////////////////////

Vec4u Vec4u::operator+(const Vec4u& other) const { return Vec4u(vaddq_u32(mIntrin, other.mIntrin)); }

Vec4u Vec4u::operator-(const Vec4u& other) const { return Vec4u(vsubq_u32(mIntrin, other.mIntrin)); }

Vec4u Vec4u::operator*(const Vec4u& other) const { return Vec4u(vmulq_u32(mIntrin, other.mIntrin)); }


///////////////////////////////
// Vec + Scalar Operators
///////////////////////////////

FX_FORCE_INLINE Vec4u Vec4u::operator*(uint32 scalar) const { return Vec4u(vmulq_n_u32(mIntrin, scalar)); }
FX_FORCE_INLINE Vec4u& Vec4u::operator*=(uint32 scalar)
{
	mIntrin = vmulq_n_u32(mIntrin, scalar);
	return *this;
}


FX_FORCE_INLINE Vec4u& Vec4u::operator+=(const Vec4u& other)
{
	mIntrin = vaddq_u32(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE Vec4u& Vec4u::operator-=(const Vec4u& other)
{
	mIntrin = vsubq_u32(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE Vec4u& Vec4u::operator*=(const Vec4u& other)
{
	mIntrin = vmulq_u32(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE Vec4u& Vec4u::operator=(const Vec4u& other)
{
	mIntrin = other.mIntrin;
	return *this;
}

} // namespace fx

#endif
