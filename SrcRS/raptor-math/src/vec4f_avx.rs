/*
 * File:        vec4f_avx.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: SSE/AVX definitions for 4 component vector (f32)
 */

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub mod vec4f_platform
{
	use crate::x86::*;

	// The lane operations that do not care how many components are live are shared with Vec3f
	pub use crate::vec3f_platform::{
		FLOAT4, abs, add, all_equal, clamp, cross, div, divs, flip_signs, get_values, is_close,
		is_zero, lerp, load, max, min, mul, mul_add, muls, neg, set_w, sub,
	};

	#[inline(always)]
	pub fn set(x: f32, y: f32, z: f32, w: f32) -> FLOAT4
	{
		unsafe { _mm_setr_ps(x, y, z, w) }
	}

	#[inline(always)]
	pub fn splat(scalar: f32) -> FLOAT4
	{
		unsafe { _mm_set1_ps(scalar) }
	}

	/// Dot product across all four lanes
	#[inline(always)]
	pub fn dot(a: FLOAT4, b: FLOAT4) -> f32
	{
		unsafe { _mm_cvtss_f32(_mm_dp_ps::<0xFF>(a, b)) }
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

	/// Divides by the length, as the C++ `SSE::Normalize` does
	#[inline(always)]
	pub fn normalize(v: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_div_ps(v, _mm_set1_ps(length(v))) }
	}
}
