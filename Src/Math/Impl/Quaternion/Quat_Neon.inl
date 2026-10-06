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
	float32 out[4];
	rx_quat_nlerp(&mData[0], &dest.mData[0], time, out);

	*this = Quat(out[0], out[1], out[2], out[3]);
}

// Based off of https://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/slerp/index.htm and optimized
// for Neon.
FX_FORCE_INLINE Quat Quat::SLerp(const Quat& dest, const float32 step) const
{
	float32 out[4];
	rx_quat_slerp(&mData[0], &dest.mData[0], step, out);

	return Quat(out[0], out[1], out[2], out[3]);
}

FX_FORCE_INLINE Quat Quat::Conjugate() const { return Quat(Neon::FlipSigns<-1, -1, -1, 1>(mIntrin)); }
FX_FORCE_INLINE Quat Quat::Normalize() const { return Quat(Neon::Normalize(mIntrin)); }

} // namespace fx

#endif
