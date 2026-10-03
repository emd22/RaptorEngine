/*
 * File:        vec2d_neon.rs
 * Author:      emd22
 * Created:     03/10/2026
 * Description: Neon definitions for 2 component vector (f64)
 */

#[cfg(target_arch = "aarch64")]
pub mod vec2d_platform
{
	use core::arch::aarch64::*;

	pub type DOUBLE2 = float64x2_t;

	#[inline(always)]
	pub fn set(x: f64, y: f64) -> DOUBLE2
	{
		let tmp_v = [x, y];
		unsafe { vld1q_f64(tmp_v.as_ptr()) }
	}

	#[inline(always)]
	pub fn splat(scalar: f64) -> DOUBLE2
	{
		unsafe { vdupq_n_f64(scalar) }
	}

	/////////////////////////////////////
	// Operators
	/////////////////////////////////////

	#[inline(always)]
	pub fn add(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe { vaddq_f64(a, b) }
	}

	#[inline(always)]
	pub fn sub(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe { vsubq_f64(a, b) }
	}

	#[inline(always)]
	pub fn mul(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe { vmulq_f64(a, b) }
	}

	#[inline(always)]
	pub fn div(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe { vdivq_f64(a, b) }
	}

	/////////////////////////////////////
	// Trig functions
	/////////////////////////////////////

	/// Get the dot product of two double vec2's with the reslt being returned in the
	/// first lane of a double vec2.

	#[inline(always)]
	pub fn dot_v(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe {
			let prod = vmulq_f64(a, b);
			// Extract and flip the vector. Similar to a normal VREV
			let rev_v = vextq_f64(prod, prod, 1);
			// Return the final dot product value (A+B, B+A)
			vaddq_f64(prod, rev_v)
		}
	}

	#[inline(always)]
	pub fn dot(a: DOUBLE2, b: DOUBLE2) -> f64
	{
		unsafe {
			let prod = vmulq_f64(a, b);
			vaddvq_f64(prod)
		}
	}
}
