/*
 * File:        vec3d_avx.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: SSE/AVX definitions for 3 component vector (f64)
 */

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub mod vec3d_platform
{
	use crate::x86::*;

	use crate::vec2d_platform as vec2d;

	pub type DOUBLE4 = __m256d;

	#[inline(always)]
	pub fn set(x: f64, y: f64, z: f64) -> DOUBLE4
	{
		unsafe { _mm256_setr_pd(x, y, z, 0.0f64) }
	}

	/// Creates a DOUBLE4 from a low pair `(x, y)` and a high pair `(z, w)`
	#[inline(always)]
	pub fn set_vectors(a: vec2d::DOUBLE2, b: vec2d::DOUBLE2) -> DOUBLE4
	{
		unsafe { _mm256_set_m128d(b, a) }
	}

	#[inline(always)]
	pub fn splat(scalar: f64) -> DOUBLE4
	{
		unsafe { _mm256_blend_pd::<0b1000>(_mm256_set1_pd(scalar), _mm256_setzero_pd()) }
	}

	/// Splits a vector into its low pair `(x, y)` and high pair `(z, w)`
	#[inline(always)]
	pub fn get_vectors(v: DOUBLE4) -> (vec2d::DOUBLE2, vec2d::DOUBLE2)
	{
		unsafe { (_mm256_castpd256_pd128(v), _mm256_extractf128_pd::<1>(v)) }
	}

	/////////////////////////////////////
	// Operators
	/////////////////////////////////////

	#[inline(always)]
	pub fn add(a: DOUBLE4, b: DOUBLE4) -> DOUBLE4
	{
		unsafe { _mm256_add_pd(a, b) }
	}

	#[inline(always)]
	pub fn sub(a: DOUBLE4, b: DOUBLE4) -> DOUBLE4
	{
		unsafe { _mm256_sub_pd(a, b) }
	}

	#[inline(always)]
	pub fn mul(a: DOUBLE4, b: DOUBLE4) -> DOUBLE4
	{
		unsafe { _mm256_mul_pd(a, b) }
	}

	#[inline(always)]
	pub fn div(a: DOUBLE4, b: DOUBLE4) -> DOUBLE4
	{
		unsafe { _mm256_div_pd(a, b) }
	}

	/// Multiplies a vector by a scalar value
	#[inline(always)]
	pub fn muls(a: DOUBLE4, b: f64) -> DOUBLE4
	{
		unsafe { _mm256_mul_pd(a, _mm256_set1_pd(b)) }
	}

	/// Divides a vector by a scalar value
	#[inline(always)]
	pub fn divs(a: DOUBLE4, b: f64) -> DOUBLE4
	{
		unsafe { _mm256_div_pd(a, _mm256_set1_pd(b)) }
	}

	#[inline(always)]
	pub fn abs(v: DOUBLE4) -> DOUBLE4
	{
		unsafe { _mm256_andnot_pd(_mm256_set1_pd(-0.0f64), v) }
	}

	/////////////////////////////////////
	// Trig functions
	/////////////////////////////////////

	/// Sums the products in the same order as the NEON version, `(x + y) + (z + w)`
	#[inline(always)]
	pub fn dot(a: DOUBLE4, b: DOUBLE4) -> f64
	{
		unsafe {
			let (a_lo, a_hi) = get_vectors(a);
			let (b_lo, b_hi) = get_vectors(b);

			let sum = _mm_add_pd(vec2d::dot_v(a_lo, b_lo), vec2d::dot_v(a_hi, b_hi));
			_mm_cvtsd_f64(sum)
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
