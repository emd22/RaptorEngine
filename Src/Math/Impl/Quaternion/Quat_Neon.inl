#pragma once

#include <Math/Quat.hpp>

#ifdef FX_USE_NEON

namespace fx {

FX_FORCE_INLINE Quat::Quat(const float32* buffer)
{
	const float aligned_buffer[] = { buffer[0], buffer[1], buffer[2], buffer[3] };
	mIntrin = vld1q_f32(aligned_buffer);
}

FX_FORCE_INLINE Quat& Quat::operator=(const float32x4_t& other)
{
	mIntrin = other;
	return *this;
}

FX_FORCE_INLINE bool Quat::IsCloseTo(const Quat& other, const float32 tolerance) const
{
	return IsCloseTo(other.mIntrin, tolerance);
}

FX_FORCE_INLINE Vec3f Quat::GetDirection() const
{
	// dir X = (2.0 * ((X * Z) + (Y * W)))
	// dir Y = (2.0 * ((Y * Z) - (X * W)))
	// dir Z = 1.0 - (2.0 * ((X * X) + (Y * Y)))

	// First hurdle, getting the term { XZ YZ } + { YW -XW }

	float32x2_t v_xy = vget_low_f32(mIntrin);

	float32 signs[] = { 1.0, -1.0 };

	// YW and -XW
	const float32x2_t yw_nxw = vmul_f32(vmul_laneq_f32(vrev64_f32(v_xy), mIntrin, 3), vld1_f32(signs));

	float32x2_t res_lo = vfma_laneq_f32(yw_nxw, v_xy, mIntrin, 2);

	// { XX YY }
	float32x2_t res_hi = vmul_f32(v_xy, v_xy);
	res_hi = vadd_f32(res_hi, vrev64_f32(res_hi));

	const float32 scale[] = { 2.0f, 2.0f, -2.0f, 0.0f };
	const float32 bias[] = { 0.0f, 0.0f, 1.0f, 0.0f };

	return Vec3f(vfmaq_f32(vld1q_f32(bias), vcombine_f32(res_lo, res_hi), vld1q_f32(scale)));
}

FX_FORCE_INLINE bool Quat::IsCloseTo(const float32x4_t& other, const float32 tolerance) const
{
	// Get the absolute difference between the vectors
	const float32x4_t diff = vabdq_f32(mIntrin, other);

	// Is the difference greater than our threshhold?
	const uint32x4_t lt = vcgtq_f32(diff, vdupq_n_f32(tolerance));

	// If any components are true, return false.
	return vmaxvq_u32(lt) == 0;
}

FX_FORCE_INLINE void Quat::NLerpIP(const Quat& dest, float32 time)
{
	// We can change (A + (B - A) * time) to ((1 - time) * A + time * B) to reduce floating point errors.

	float32x4_t dest_v = dest.mIntrin;

	if (Neon::Dot(mIntrin, dest.mIntrin) < 0.0f) {
		dest_v = Neon::SetSigns<-1>(dest_v);
	}

	const float32x4_t inv_time_v = vdupq_n_f32(1.0f - time);
	const float32x4_t time_v = vdupq_n_f32(time);

	// (1 - time) * A
	mIntrin = vmulq_f32(inv_time_v, mIntrin);
	// ... + time * B -> vfmaq takes in (D, N, M); N and M are multiplied, and accumulated to D(destination).
	mIntrin = vfmaq_f32(mIntrin, time_v, dest_v);

	// Quaternion lerp is always normalized!
	mIntrin = Neon::Normalize(mIntrin);
}

// Based off of https://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/slerp/index.htm and optimized
// for Neon.
FX_FORCE_INLINE Quat Quat::SLerp(const Quat& dest, const float32 step) const
{
	// Note: there are so many different ways to implement this and a lot of them online just straight up pro
	float32x4_t a_v = mIntrin;
	float32x4_t b_v = dest.mIntrin;

	float32x4_t result;

	// Calculate angle between them.
	float32 cos_half_theta = Neon::Dot(a_v, b_v);

	// quat and -quat represent the same rotation; if they're in opposite hemispheres, negate one so we take the
	// shortest path instead of interpolating the long way around (matches the check already done in NLerpIP above).
	if (cos_half_theta < 0.0f) {
		b_v = Neon::SetSigns<-1>(b_v);
		cos_half_theta = -cos_half_theta;
	}

	// if qa=qb or qa=-qb then theta = 0 and we can return qa
	if (abs(cos_half_theta) >= 1.0f) {
		return Quat(mIntrin);
	}

	// Calculate temporary values.
	float32 half_theta = acosf(cos_half_theta);
	float32 sin_half_theta = sqrtf(1.0f - cos_half_theta * cos_half_theta);

	// if theta = 180 degrees then result is not fully defined
	// we could rotate around any axis normal to qa or qb
	if (sin_half_theta < 0.001f) {
		float32x4_t half = vdupq_n_f32(0.5f);

		result = vmulq_f32(a_v, half);
		result = vfmaq_f32(result, b_v, half);

		return Quat(result);
	}

	const float32 sht_recip = 1.0f / sin_half_theta;
	float32 ratioA = sinf((1.0f - step) * half_theta) * sht_recip;
	float32 ratioB = sinf(step * half_theta) * sht_recip;

	result = vmulq_f32(a_v, vdupq_n_f32(ratioA));
	result = vfmaq_f32(result, b_v, vdupq_n_f32(ratioB));

	return Quat(result);
}

FX_FORCE_INLINE Quat Quat::Conjugate() const { return Quat(Neon::FlipSigns<-1, -1, -1, 1>(mIntrin)); }
FX_FORCE_INLINE Quat Quat::Normalize() const { return Quat(Neon::Normalize(mIntrin)); }

} // namespace fx

#endif
