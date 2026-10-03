/*
 * File:        vec3d_neon.rs
 * Author:      emd22
 * Created:     03/10/2026
 * Description: Neon definitions for 3 component vector (f64)
 */

#[cfg(target_arch = "aarch64")]
pub mod vec3d_platform
{
	use core::arch::aarch64::*;

	use crate::vec2d_platform as vec2d;

	pub type DOUBLE4 = float64x2x2_t;

	#[inline(always)]
	pub fn set(x: f64, y: f64, z: f64) -> DOUBLE4
	{
		let tmp_v = [x, y, z, 0.0f64];
		unsafe { vld1q_f64_x2(tmp_v.as_ptr()) }
	}

	/// Creates a DOUBLE4 from
	#[inline(always)]
	pub fn set_vectors(a: vec2d::DOUBLE2, b: vec2d::DOUBLE2) -> DOUBLE4
	{
		float64x2x2_t(a, b)
	}

	#[inline(always)]
	pub fn splat(scalar: f64) -> DOUBLE4
	{
		unsafe { float64x2x2_t(vdupq_n_f64(scalar), vsetq_lane_f64(0.0f64, vdupq_n_f64(scalar), 1)) }
	}

	/////////////////////////////////////
	// Operators
	/////////////////////////////////////

	#[inline(always)]
	pub fn add(a: DOUBLE4, b: DOUBLE4) -> DOUBLE4
	{
		unsafe { float64x2x2_t(vaddq_f64(a.0, b.0), vaddq_f64(a.1, b.1)) }
	}

	#[inline(always)]
	pub fn sub(a: DOUBLE4, b: DOUBLE4) -> DOUBLE4
	{
		unsafe { float64x2x2_t(vsubq_f64(a.0, b.0), vsubq_f64(a.1, b.1)) }
	}

	#[inline(always)]
	pub fn mul(a: DOUBLE4, b: DOUBLE4) -> DOUBLE4
	{
		unsafe { float64x2x2_t(vmulq_f64(a.0, b.0), vmulq_f64(a.1, b.1)) }
	}

	#[inline(always)]
	pub fn div(a: DOUBLE4, b: DOUBLE4) -> DOUBLE4
	{
		unsafe { float64x2x2_t(vdivq_f64(a.0, b.0), vdivq_f64(a.1, b.1)) }
	}

	/// Multiplies a vector by a scalar value
	#[inline(always)]
	pub fn muls(a: DOUBLE4, b: f64) -> DOUBLE4
	{
		unsafe { float64x2x2_t(vmulq_n_f64(a.0, b), vmulq_n_f64(a.1, b)) }
	}

	/// Divides a vector by a scalar value
	#[inline(always)]
	pub fn divs(a: DOUBLE4, b: f64) -> DOUBLE4
	{
		unsafe { float64x2x2_t(vdivq_f64(a.0, vdupq_n_f64(b)), vdivq_f64(a.1, vdupq_n_f64(b))) }
	}

	#[inline(always)]
	pub fn abs(v: DOUBLE4) -> DOUBLE4
	{
		unsafe { float64x2x2_t(vabsq_f64(v.0), vabsq_f64(v.1)) }
	}

	/////////////////////////////////////
	// Trig functions
	/////////////////////////////////////

	#[inline(always)]
	pub fn dot(a: DOUBLE4, b: DOUBLE4) -> f64
	{
		unsafe {
			let result: vec2d::DOUBLE2 = vaddq_f64(vec2d::dot_v(a.0, b.0), vec2d::dot_v(a.1, b.1));
			vgetq_lane_f64(result, 0)
		}
	}

	#[inline(always)]
	pub fn length_sq(v: DOUBLE4) -> f64
	{
		dot(v, v)
	}

	#[inline(always)]
	pub fn length(v: DOUBLE4) -> f64
	{
		length_sq(v).sqrt()
	}
}
