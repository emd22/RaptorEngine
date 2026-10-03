/*
 * File:        vec3f_neon.rs
 * Author:      emd22
 * Created:     03/10/2026
 * Description: Neon definitions for 3 component vector (f32)
 */

#[cfg(target_arch = "aarch64")]
pub mod vec3f_platform
{
	use core::arch::aarch64::*;

	pub type FLOAT4 = float32x4_t;

	#[inline(always)]
	pub fn set(x: f32, y: f32, z: f32) -> FLOAT4
	{
		let tmp_v = [x, y, z, 0.0f32];
		unsafe { vld1q_f32(tmp_v.as_ptr()) }
	}

	#[inline(always)]
	pub fn splat(scalar: f32) -> FLOAT4
	{
		unsafe { vsetq_lane_f32(0.0f32, vdupq_n_f32(scalar), 3) }
	}

	#[inline(always)]
	pub fn get_values(a: FLOAT4) -> [f32; 4]
	{
		let mut values = [0.0f32; 4];
		unsafe {
			vst1q_f32(values.as_mut_ptr(), a);
		}
		values
	}

	/////////////////////////////////////
	// Operators
	/////////////////////////////////////

	#[inline(always)]
	pub fn add(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { vaddq_f32(a, b) }
	}

	#[inline(always)]
	pub fn sub(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { vsubq_f32(a, b) }
	}

	#[inline(always)]
	pub fn mul(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { vmulq_f32(a, b) }
	}

	/// Multiplies a vector by a scalar value
	#[inline(always)]
	pub fn muls(a: FLOAT4, b: f32) -> FLOAT4
	{
		unsafe { vmulq_n_f32(a, b) }
	}

	/// Divides a vector by another vector
	#[inline(always)]
	pub fn div(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { vdivq_f32(a, b) }
	}

	/// Divides a vector by a scalar value
	#[inline(always)]
	pub fn divs(a: FLOAT4, b: f32) -> FLOAT4
	{
		unsafe { vmulq_f32(a, vdupq_n_f32(1.0f32 / b)) }
	}

	#[inline(always)]
	pub fn recip(v: FLOAT4) -> FLOAT4
	{
		unsafe { vrecpeq_f32(v) }
	}

	#[inline(always)]
	pub fn abs(v: FLOAT4) -> FLOAT4
	{
		unsafe { vabsq_f32(v) }
	}

	#[inline(always)]
	pub fn min(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { vminq_f32(a, b) }
	}

	#[inline(always)]
	pub fn max(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { vmaxq_f32(a, b) }
	}

	/// Returns true if ALL lanes of a vector are equal.
	#[inline(always)]
	pub fn all_equal(a: FLOAT4, b: FLOAT4) -> bool
	{
		unsafe {
			let mask = vceqq_f32(a, b);
			vminvq_u32(mask) == u32::MAX
		}
	}

	/////////////////////////////////////
	// Trig functions
	/////////////////////////////////////

	#[inline(always)]
	pub fn dot(a: FLOAT4, b: FLOAT4) -> f32
	{
		unsafe {
			let prod = vmulq_f32(a, b);
			vaddvq_f32(prod)
		}
	}

	#[inline(always)]
	pub fn length_sq(v: FLOAT4) -> f32
	{
		dot(v, v)
	}

	#[inline(always)]
	pub fn length(v: FLOAT4) -> f32
	{
		length_sq(v).sqrt()
	}

	#[inline(always)]
	pub fn normalize(v: FLOAT4) -> FLOAT4
	{
		let v_len_recip = 1.0f32 / length(v);

		// Multiply the reciprocal. This is equivalent to dividing by the length.
		unsafe { vmulq_n_f32(v, v_len_recip) }
	}

	/////////////////////////////////////
	// Utility functions
	/////////////////////////////////////

	/// Gets the sign of of a vector, returning a vector with each component either 1.0 if positive,
	/// or -1.0 if negative.
	#[inline(always)]
	pub fn get_sign(v: FLOAT4) -> FLOAT4
	{
		const SIGN_MASK: u32 = 0x80000000u32;

		unsafe {
			let v_sign_mask = vdupq_n_u32(SIGN_MASK);
			let v_one = vdupq_n_u32((1.0f32).to_bits());

			// Get the bare sign bit (-0.0, 0.0)
			let v_sign = vandq_u32(vreinterpretq_u32_f32(v), v_sign_mask);

			// OR the sign with 1.0 to get either 1.0 or -1.0
			vreinterpretq_f32_u32(vorrq_u32(v_sign, v_one))
		}
	}
}
