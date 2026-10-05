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

	/// Dot product of the first three lanes. W is masked out so a stray value there (a NaN from a
	/// lane-wise divide, say) cannot leak into the result.
	#[inline(always)]
	pub fn dot(a: FLOAT4, b: FLOAT4) -> f32
	{
		unsafe {
			let prod = vmulq_f32(a, b);
			vaddvq_f32(vsetq_lane_f32(0.0f32, prod, 3))
		}
	}

	/// Fused `a * b + accum` on every lane
	#[inline(always)]
	pub fn mul_add(a: FLOAT4, b: FLOAT4, accum: FLOAT4) -> FLOAT4
	{
		unsafe { vfmaq_f32(accum, a, b) }
	}

	#[inline(always)]
	pub fn neg(v: FLOAT4) -> FLOAT4
	{
		unsafe { vnegq_f32(v) }
	}

	/// Cross product. W is zero in the result when it is zero in both inputs.
	#[inline(always)]
	pub fn cross(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe {
			let a_yzxw = permute_yzxw(a);
			let b_yzxw = permute_yzxw(b);

			let result_yzxw = vsubq_f32(vmulq_f32(a, b_yzxw), vmulq_f32(a_yzxw, b));

			permute_yzxw(result_yzxw)
		}
	}

	/// Rearranges `(x, y, z, w)` to `(y, z, x, w)`
	#[inline(always)]
	pub fn permute_yzxw(v: FLOAT4) -> FLOAT4
	{
		unsafe {
			// (y, z, w, x)
			let rotated = vextq_f32::<1>(v, v);
			vcombine_f32(vget_low_f32(rotated), vrev64_f32(vget_high_f32(rotated)))
		}
	}

	#[inline(always)]
	pub fn lerp(a: FLOAT4, b: FLOAT4, f: f32) -> FLOAT4
	{
		unsafe { vaddq_f32(a, vmulq_n_f32(vsubq_f32(b, a), f)) }
	}

	#[inline(always)]
	pub fn clamp(v: FLOAT4, lo: FLOAT4, hi: FLOAT4) -> FLOAT4
	{
		unsafe { vminq_f32(vmaxq_f32(v, lo), hi) }
	}

	/// Returns true if every lane of `a` is within `tolerance` of the same lane of `b`
	#[inline(always)]
	pub fn is_close(a: FLOAT4, b: FLOAT4, tolerance: f32) -> bool
	{
		unsafe {
			let exceeds = vcgtq_f32(vabdq_f32(a, b), vdupq_n_f32(tolerance));
			vmaxvq_u32(exceeds) == 0
		}
	}

	/// Returns true if every lane is exactly zero
	#[inline(always)]
	pub fn is_zero(v: FLOAT4) -> bool
	{
		unsafe { vminvq_u32(vceqzq_f32(v)) == u32::MAX }
	}

	/// Flips the sign of each lane whose flag is false, matching the C++ `FlipSigns<TX, TY, TZ, TW>`
	/// where a positive template argument keeps the lane as it is.
	#[inline(always)]
	pub fn flip_signs(v: FLOAT4, keep: [bool; 4]) -> FLOAT4
	{
		const FLIP: u32 = 0x80000000u32;

		let mask = [
			if keep[0] { 0 } else { FLIP },
			if keep[1] { 0 } else { FLIP },
			if keep[2] { 0 } else { FLIP },
			if keep[3] { 0 } else { FLIP },
		];

		unsafe {
			let mask_v = vld1q_u32(mask.as_ptr());
			vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(v), mask_v))
		}
	}

	#[inline(always)]
	pub fn set_w(v: FLOAT4, w: f32) -> FLOAT4
	{
		unsafe { vsetq_lane_f32(w, v, 3) }
	}

	/// Loads four lanes from a slice, padding with zeros if the slice is shorter than four
	#[inline(always)]
	pub fn load(values: &[f32]) -> FLOAT4
	{
		let mut lanes = [0.0f32; 4];
		let count = values.len().min(4);
		lanes[..count].copy_from_slice(&values[..count]);

		unsafe { vld1q_f32(lanes.as_ptr()) }
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
