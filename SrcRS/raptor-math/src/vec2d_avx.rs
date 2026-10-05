/*
 * File:        vec2d_avx.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: SSE/AVX definitions for 2 component vector (f64)
 */

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub mod vec2d_platform
{
	use crate::x86::*;

	pub type DOUBLE2 = __m128d;

	#[inline(always)]
	pub fn set(x: f64, y: f64) -> DOUBLE2
	{
		unsafe { _mm_setr_pd(x, y) }
	}

	#[inline(always)]
	pub fn splat(scalar: f64) -> DOUBLE2
	{
		unsafe { _mm_set1_pd(scalar) }
	}

	/////////////////////////////////////
	// Operators
	/////////////////////////////////////

	#[inline(always)]
	pub fn add(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe { _mm_add_pd(a, b) }
	}

	#[inline(always)]
	pub fn sub(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe { _mm_sub_pd(a, b) }
	}

	#[inline(always)]
	pub fn mul(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe { _mm_mul_pd(a, b) }
	}

	#[inline(always)]
	pub fn div(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe { _mm_div_pd(a, b) }
	}

	/////////////////////////////////////
	// Trig functions
	/////////////////////////////////////

	/// The dot product of two double vec2's, in both lanes of a double vec2.
	#[inline(always)]
	pub fn dot_v(a: DOUBLE2, b: DOUBLE2) -> DOUBLE2
	{
		unsafe {
			let prod = _mm_mul_pd(a, b);
			let swapped = _mm_shuffle_pd::<0b01>(prod, prod);

			_mm_add_pd(prod, swapped)
		}
	}

	#[inline(always)]
	pub fn dot(a: DOUBLE2, b: DOUBLE2) -> f64
	{
		unsafe { _mm_cvtsd_f64(dot_v(a, b)) }
	}
}
