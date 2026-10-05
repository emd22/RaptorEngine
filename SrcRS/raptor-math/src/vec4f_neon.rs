/*
 * File:        vec4f_neon.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: Neon definitions for 4 component vector (f32)
 */

#[cfg(target_arch = "aarch64")]
pub mod vec4f_platform
{
	use core::arch::aarch64::*;

	// The lane operations that do not care how many components are live are shared with Vec3f
	pub use crate::vec3f_platform::{
		FLOAT4, abs, add, all_equal, clamp, cross, div, divs, flip_signs, get_values, is_close,
		is_zero, lerp, load, max, min, mul, mul_add, muls, neg, set_w, sub,
	};

	#[inline(always)]
	pub fn set(x: f32, y: f32, z: f32, w: f32) -> FLOAT4
	{
		let tmp_v = [x, y, z, w];
		unsafe { vld1q_f32(tmp_v.as_ptr()) }
	}

	#[inline(always)]
	pub fn splat(scalar: f32) -> FLOAT4
	{
		unsafe { vdupq_n_f32(scalar) }
	}

	/// Dot product across all four lanes
	#[inline(always)]
	pub fn dot(a: FLOAT4, b: FLOAT4) -> f32
	{
		unsafe { vaddvq_f32(vmulq_f32(a, b)) }
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

	/// Divides by the length, as the C++ `Neon::Normalize` does
	#[inline(always)]
	pub fn normalize(v: FLOAT4) -> FLOAT4
	{
		unsafe { vdivq_f32(v, vdupq_n_f32(length(v))) }
	}

	struct SignsTypes<const L0: i32, const L1: i32, const L2: i32, const L3: i32> {}

	#[inline(always)]
	pub fn set_signs<const L0: i32, const L1: i32, const L2: i32, const L3: i32>(
		vec: FLOAT4,
	) -> FLOAT4
	{
		unsafe {
			let masks: [u32; 4] = [
				if L0 < 0 { 0x80000000 } else { 0 },
				if L1 < 0 { 0x80000000 } else { 0 },
				if L2 < 0 { 0x80000000 } else { 0 },
				if L3 < 0 { 0x80000000 } else { 0 },
			];

			let v_signs: uint32x4_t = vld1q_u32(masks.as_ptr());
			let v_result = veorq_u32(v_signs, vreinterpretq_u32_f32(vec));

			vreinterpretq_f32_u32(v_result)
		}
	}
}
