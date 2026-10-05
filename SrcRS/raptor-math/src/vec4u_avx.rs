/*
 * File:        vec4u_avx.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: SSE/AVX definitions for 4 component vector (u32)
 */

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub mod vec4u_platform
{
	use crate::x86::*;

	pub type UINT4 = __m128i;

	#[inline(always)]
	pub fn set(x: u32, y: u32, z: u32, w: u32) -> UINT4
	{
		unsafe { _mm_setr_epi32(x as i32, y as i32, z as i32, w as i32) }
	}

	#[inline(always)]
	pub fn splat(scalar: u32) -> UINT4
	{
		unsafe { _mm_set1_epi32(scalar as i32) }
	}

	#[inline(always)]
	pub fn get_values(a: UINT4) -> [u32; 4]
	{
		let mut values = [0u32; 4];
		unsafe {
			_mm_storeu_si128(values.as_mut_ptr() as *mut __m128i, a);
		}
		values
	}

	#[inline(always)]
	pub fn add(a: UINT4, b: UINT4) -> UINT4
	{
		unsafe { _mm_add_epi32(a, b) }
	}

	#[inline(always)]
	pub fn sub(a: UINT4, b: UINT4) -> UINT4
	{
		unsafe { _mm_sub_epi32(a, b) }
	}

	#[inline(always)]
	pub fn mul(a: UINT4, b: UINT4) -> UINT4
	{
		unsafe { _mm_mullo_epi32(a, b) }
	}

	#[inline(always)]
	pub fn muls(a: UINT4, b: u32) -> UINT4
	{
		unsafe { _mm_mullo_epi32(a, _mm_set1_epi32(b as i32)) }
	}

	#[inline(always)]
	pub fn is_zero(v: UINT4) -> bool
	{
		unsafe { _mm_testz_si128(v, v) == 1 }
	}

	#[inline(always)]
	pub fn all_equal(a: UINT4, b: UINT4) -> bool
	{
		unsafe { _mm_movemask_epi8(_mm_cmpeq_epi32(a, b)) == 0xFFFF }
	}
}
