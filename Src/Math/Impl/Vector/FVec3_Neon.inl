#pragma once

#include <Core/Defines.hpp>

#ifdef FX_USE_NEON


#include <arm_neon.h>

#include <Math/FVec3.hpp>
#include <Math/SIMDHelper.hpp>

namespace fx {

FX_FORCE_INLINE FVec3::FVec3(float32 x, float32 y, float32 z, float32 w)
{
	const float32 values[4] = { x, y, z, w };
	mIntrin = vld1q_f32(values);
}

FX_FORCE_INLINE FVec3::FVec3(const float32* values)
{
	const float32 values4[4] = { values[0], values[1], values[2], 0.0f };
	mIntrin = vld1q_f32(values4);
}

FX_FORCE_INLINE FVec3::FVec3(float32 scalar) { mIntrin = vsetq_lane_f32(0.0f, vdupq_n_f32(scalar), 3); }

FX_FORCE_INLINE bool FVec3::IsCloseTo(const FVec3 other, const float32 tolerance) const
{
	return IsCloseTo(other.mIntrin, tolerance);
}

FX_FORCE_INLINE FVec3 FVec3::MulAdd(const FVec3 a, const FVec3 b, const FVec3 accum)
{
	return FVec3(vfmaq_f32(accum.mIntrin, a.mIntrin, b.mIntrin));
}


FX_FORCE_INLINE bool FVec3::IsCloseTo(const FLOAT4 other, const float32 tolerance) const
{
	// Get the absolute difference between the vectors
	const float32x4_t diff = vabdq_f32(mIntrin, other);

	// Is the difference greater than our threshold?
	const uint32x4_t lt = vcgtq_f32(diff, vdupq_n_f32(tolerance));

	// If any components are true, return false.
	return vmaxvq_u32(lt) == 0;
}

FX_FORCE_INLINE bool FVec3::IsZero() const
{
	const uint32x4_t is_zero_v = vceqzq_f32(mIntrin);
	return vmaxvq_u32(is_zero_v) == 0;
}

FX_FORCE_INLINE bool FVec3::IsNearZero(const float32 tolerance) const { return IsCloseTo(sZero, tolerance); }


FX_FORCE_INLINE void FVec3::Set(float32 x, float32 y, float32 z)
{
	const float32 v[] = { x, y, z, 0 };
	mIntrin = vld1q_f32(v);
}

FX_FORCE_INLINE FVec3 FVec3::Min(const FVec3 a, const FVec3 b) { return FVec3(vminq_f32(a.mIntrin, b.mIntrin)); }

FX_FORCE_INLINE FVec3 FVec3::Max(const FVec3 a, const FVec3 b) { return FVec3(vmaxq_f32(a.mIntrin, b.mIntrin)); }

FX_FORCE_INLINE FVec3 FVec3::Clamp(const FVec3 v, const FVec3 min, const FVec3 max)
{
	return FVec3(vminq_f32(vmaxq_f32(v.mIntrin, min.mIntrin), max.mIntrin));
}

FX_FORCE_INLINE FVec3 FVec3::Lerp(const FVec3 a, const FVec3 b, const float f)
{
	// a + f * (b - a);
	float32x4_t d = vsubq_f32(b.mIntrin, a.mIntrin);

	return FVec3(vaddq_f32(a.mIntrin, vmulq_n_f32(d, f)));
}

FX_FORCE_INLINE FVec3& FVec3::LerpIP(const FVec3 dest, const float step)
{
	// a + f * (b - a);
	float32x4_t d = vsubq_f32(dest.mIntrin, mIntrin);

	mIntrin = vaddq_f32(mIntrin, vmulq_n_f32(d, step));

	return *this;
}


FX_FORCE_INLINE FVec3& FVec3::NormalizeIP()
{
	// Calculate length and splat to register
	const float32x4_t len_v = vdupq_n_f32(Length());

	// Divide vector by length
	mIntrin = vdivq_f32(mIntrin, len_v);

	return *this;
}

FX_FORCE_INLINE FVec3 FVec3::Normalize() const
{
	// Calculate length and splat to register
	const float32x4_t len_v = vdupq_n_f32(Length());

	return FVec3(vdivq_f32(mIntrin, len_v));
}

FX_FORCE_INLINE FVec3 FVec3::SafeNormalize(const FVec3 fallback) const
{
	static constexpr float32 scEpsilon = 1e-5f;

	const float32 len = Length();

	if (len < scEpsilon) {
		return fallback;
	}

	return FVec3(vdivq_f32(mIntrin, vdupq_n_f32(len)));
}


FX_FORCE_INLINE float32 FVec3::Length() const
{
	float32x4_t v = mIntrin;
	return sqrtf(Dot(v));
}

FX_FORCE_INLINE float32 FVec3::Dot(const FVec3 other) const
{
	return Neon::Dot(vsetq_lane_f32(0.0f, mIntrin, 3), vsetq_lane_f32(0.0f, other.mIntrin, 3));
}

FX_FORCE_INLINE FVec3 FVec3::Cross(const FVec3 other) const { return FVec3(Neon::Cross(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE float32 FVec3::Dot(FLOAT4 other) const
{
	return Neon::Dot(vsetq_lane_f32(0.0f, mIntrin, 3), vsetq_lane_f32(0.0f, other, 3));
}

//////////////////////////////
// Operator Overloads
//////////////////////////////

FX_FORCE_INLINE bool FVec3::operator==(const FVec3 other) const
{
	const uint32x4_t equal_mask = vsetq_lane_u32(UINT32_MAX, vceqq_f32(mIntrin, other.mIntrin), 3);
	return vminvq_u32(equal_mask) == UINT32_MAX;
}

FX_FORCE_INLINE bool FVec3::operator>(const FVec3 other) const
{
	const uint32x4_t mask = vsetq_lane_u32(UINT32_MAX, vcgtq_f32(mIntrin, other.mIntrin), 3);
	return vminvq_u32(mask) == UINT32_MAX;
}

FX_FORCE_INLINE bool FVec3::operator<(const FVec3 other) const
{
	const uint32x4_t mask = vsetq_lane_u32(UINT32_MAX, vcltq_f32(mIntrin, other.mIntrin), 3);
	return vminvq_u32(mask) == UINT32_MAX;
}

FX_FORCE_INLINE bool FVec3::operator>=(const FVec3 other) const
{
	const uint32x4_t mask = vsetq_lane_u32(UINT32_MAX, vcgeq_f32(mIntrin, other.mIntrin), 3);
	return vminvq_u32(mask) == UINT32_MAX;
}

FX_FORCE_INLINE bool FVec3::operator<=(const FVec3 other) const
{
	const uint32x4_t mask = vsetq_lane_u32(UINT32_MAX, vcleq_f32(mIntrin, other.mIntrin), 3);
	return vminvq_u32(mask) == UINT32_MAX;
}

FX_FORCE_INLINE FVec3 FVec3::operator+(const FVec3 other) const { return FVec3(vaddq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE FVec3 FVec3::operator-(const FVec3 other) const { return FVec3(vsubq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE FVec3 FVec3::operator*(const FVec3 other) const { return FVec3(vmulq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE FVec3 FVec3::operator*(float32 scalar) const
{
	float32x4_t result = vmulq_n_f32(mIntrin, scalar);
	return FVec3(result);
}

FX_FORCE_INLINE FVec3 FVec3::operator/(const FVec3 other) const { return FVec3(vdivq_f32(mIntrin, other.mIntrin)); }

FX_FORCE_INLINE FVec3 FVec3::operator/(float32 scalar) const
{
	float32x4_t result = vdivq_f32(mIntrin, vdupq_n_f32(scalar));
	return FVec3(result);
}

FX_FORCE_INLINE FVec3 FVec3::operator-() const
{
	float32x4_t result = vnegq_f32(mIntrin);
	return FVec3(result);
}


FX_FORCE_INLINE FVec3& FVec3::operator+=(const FVec3 other)
{
	mIntrin = vaddq_f32(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec3& FVec3::operator-=(const FVec3 other)
{
	mIntrin = vsubq_f32(mIntrin, other.mIntrin);
	return *this;
}

FX_FORCE_INLINE FVec3& FVec3::operator-=(float32 scalar)
{
	mIntrin = vsubq_f32(mIntrin, vdupq_n_f32(scalar));
	return *this;
}

FX_FORCE_INLINE FVec3& FVec3::operator*=(const FVec3 other)
{
	mIntrin = vmulq_f32(mIntrin, other.mIntrin);
	return *this;
}

} // namespace fx

#endif
