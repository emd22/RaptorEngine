/*
 * File:        vec3f_avx.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: SSE/AVX definitions for 3 component vector (f32)
 */

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub mod vec3f_platform
{
	use crate::x86::*;

	pub type FLOAT4 = __m128;

	const SIGN_BIT: i32 = i32::MIN;

	#[inline(always)]
	fn sign_mask() -> __m128
	{
		unsafe { _mm_castsi128_ps(_mm_set1_epi32(SIGN_BIT)) }
	}

	#[inline(always)]
	pub fn set(x: f32, y: f32, z: f32) -> FLOAT4
	{
		unsafe { _mm_setr_ps(x, y, z, 0.0f32) }
	}

	#[inline(always)]
	pub fn splat(scalar: f32) -> FLOAT4
	{
		unsafe { _mm_blend_ps::<0b1000>(_mm_set1_ps(scalar), _mm_setzero_ps()) }
	}

	#[inline(always)]
	pub fn get_values(a: FLOAT4) -> [f32; 4]
	{
		let mut values = [0.0f32; 4];
		unsafe {
			_mm_storeu_ps(values.as_mut_ptr(), a);
		}
		values
	}

	/////////////////////////////////////
	// Operators
	/////////////////////////////////////

	#[inline(always)]
	pub fn add(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_add_ps(a, b) }
	}

	#[inline(always)]
	pub fn sub(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_sub_ps(a, b) }
	}

	#[inline(always)]
	pub fn mul(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_mul_ps(a, b) }
	}

	/// Multiplies a vector by a scalar value
	#[inline(always)]
	pub fn muls(a: FLOAT4, b: f32) -> FLOAT4
	{
		unsafe { _mm_mul_ps(a, _mm_set1_ps(b)) }
	}

	/// Divides a vector by another vector
	#[inline(always)]
	pub fn div(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_div_ps(a, b) }
	}

	/// Divides a vector by a scalar value
	#[inline(always)]
	pub fn divs(a: FLOAT4, b: f32) -> FLOAT4
	{
		unsafe { _mm_div_ps(a, _mm_set1_ps(b)) }
	}

	/// An estimate of the reciprocal of each lane
	#[inline(always)]
	pub fn recip(v: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_rcp_ps(v) }
	}

	#[inline(always)]
	pub fn abs(v: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_andnot_ps(sign_mask(), v) }
	}

	#[inline(always)]
	pub fn min(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_min_ps(a, b) }
	}

	#[inline(always)]
	pub fn max(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_max_ps(a, b) }
	}

	/// Returns true if ALL lanes of a vector are equal.
	#[inline(always)]
	pub fn all_equal(a: FLOAT4, b: FLOAT4) -> bool
	{
		unsafe { _mm_movemask_ps(_mm_cmpeq_ps(a, b)) == 0b1111 }
	}

	/////////////////////////////////////
	// Trig functions
	/////////////////////////////////////

	/// Dot product of the first three lanes. W is masked out so a stray value there (a NaN from a
	/// lane-wise divide, say) cannot leak into the result.
	#[inline(always)]
	pub fn dot(a: FLOAT4, b: FLOAT4) -> f32
	{
		unsafe { _mm_cvtss_f32(_mm_dp_ps::<0b0111_0111>(a, b)) }
	}

	/// Fused `a * b + accum` on every lane
	#[inline(always)]
	pub fn mul_add(a: FLOAT4, b: FLOAT4, accum: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_fmadd_ps(a, b, accum) }
	}

	#[inline(always)]
	pub fn neg(v: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_xor_ps(v, sign_mask()) }
	}

	/// Cross product. W is zero in the result when it is zero in both inputs.
	#[inline(always)]
	pub fn cross(a: FLOAT4, b: FLOAT4) -> FLOAT4
	{
		unsafe {
			let a_yzxw = permute_yzxw(a);
			let b_yzxw = permute_yzxw(b);

			let result_yzxw = _mm_sub_ps(_mm_mul_ps(a, b_yzxw), _mm_mul_ps(a_yzxw, b));

			permute_yzxw(result_yzxw)
		}
	}

	/// Rearranges `(x, y, z, w)` to `(y, z, x, w)`
	#[inline(always)]
	pub fn permute_yzxw(v: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_shuffle_ps::<0b11_00_10_01>(v, v) }
	}

	#[inline(always)]
	pub fn lerp(a: FLOAT4, b: FLOAT4, f: f32) -> FLOAT4
	{
		unsafe { _mm_add_ps(a, _mm_mul_ps(_mm_sub_ps(b, a), _mm_set1_ps(f))) }
	}

	#[inline(always)]
	pub fn clamp(v: FLOAT4, lo: FLOAT4, hi: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_min_ps(_mm_max_ps(v, lo), hi) }
	}

	/// Returns true if every lane of `a` is within `tolerance` of the same lane of `b`
	#[inline(always)]
	pub fn is_close(a: FLOAT4, b: FLOAT4, tolerance: f32) -> bool
	{
		unsafe {
			let diff = abs(_mm_sub_ps(a, b));
			_mm_movemask_ps(_mm_cmpgt_ps(diff, _mm_set1_ps(tolerance))) == 0
		}
	}

	/// Returns true if every lane is exactly zero
	#[inline(always)]
	pub fn is_zero(v: FLOAT4) -> bool
	{
		unsafe { _mm_movemask_ps(_mm_cmpeq_ps(v, _mm_setzero_ps())) == 0b1111 }
	}

	/// Flips the sign of each lane whose flag is false, matching the C++ `FlipSigns<TX, TY, TZ, TW>`
	/// where a positive template argument keeps the lane as it is.
	#[inline(always)]
	pub fn flip_signs(v: FLOAT4, keep: [bool; 4]) -> FLOAT4
	{
		let lane = |keep: bool| if keep { 0 } else { SIGN_BIT };

		unsafe {
			let mask = _mm_setr_epi32(lane(keep[0]), lane(keep[1]), lane(keep[2]), lane(keep[3]));
			_mm_xor_ps(v, _mm_castsi128_ps(mask))
		}
	}

	#[inline(always)]
	pub fn set_w(v: FLOAT4, w: f32) -> FLOAT4
	{
		unsafe { _mm_blend_ps::<0b1000>(v, _mm_set1_ps(w)) }
	}

	/// Loads four lanes from a slice, padding with zeros if the slice is shorter than four
	#[inline(always)]
	pub fn load(values: &[f32]) -> FLOAT4
	{
		let mut lanes = [0.0f32; 4];
		let count = values.len().min(4);
		lanes[..count].copy_from_slice(&values[..count]);

		unsafe { _mm_loadu_ps(lanes.as_ptr()) }
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
		unsafe { _mm_div_ps(v, _mm_set1_ps(length(v))) }
	}

	/////////////////////////////////////
	// Utility functions
	/////////////////////////////////////

	/// Gets the sign of of a vector, returning a vector with each component either 1.0 if positive,
	/// or -1.0 if negative.
	#[inline(always)]
	pub fn get_sign(v: FLOAT4) -> FLOAT4
	{
		unsafe { _mm_or_ps(_mm_and_ps(v, sign_mask()), _mm_set1_ps(1.0f32)) }
	}
}
