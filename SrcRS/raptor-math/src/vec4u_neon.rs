/*
 * File:        vec4u_neon.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: Neon definitions for 4 component vector (u32)
 */

#[cfg(target_arch = "aarch64")]
pub mod vec4u_platform
{
	use core::arch::aarch64::*;

	pub type UINT4 = uint32x4_t;

	#[inline(always)]
	pub fn set(x: u32, y: u32, z: u32, w: u32) -> UINT4
	{
		let tmp_v = [x, y, z, w];
		unsafe { vld1q_u32(tmp_v.as_ptr()) }
	}

	#[inline(always)]
	pub fn splat(scalar: u32) -> UINT4
	{
		unsafe { vdupq_n_u32(scalar) }
	}

	#[inline(always)]
	pub fn get_values(a: UINT4) -> [u32; 4]
	{
		let mut values = [0u32; 4];
		unsafe {
			vst1q_u32(values.as_mut_ptr(), a);
		}
		values
	}

	#[inline(always)]
	pub fn add(a: UINT4, b: UINT4) -> UINT4
	{
		unsafe { vaddq_u32(a, b) }
	}

	#[inline(always)]
	pub fn sub(a: UINT4, b: UINT4) -> UINT4
	{
		unsafe { vsubq_u32(a, b) }
	}

	#[inline(always)]
	pub fn mul(a: UINT4, b: UINT4) -> UINT4
	{
		unsafe { vmulq_u32(a, b) }
	}

	#[inline(always)]
	pub fn muls(a: UINT4, b: u32) -> UINT4
	{
		unsafe { vmulq_n_u32(a, b) }
	}

	#[inline(always)]
	pub fn is_zero(v: UINT4) -> bool
	{
		unsafe { vminvq_u32(vceqzq_u32(v)) == u32::MAX }
	}

	#[inline(always)]
	pub fn all_equal(a: UINT4, b: UINT4) -> bool
	{
		unsafe { vminvq_u32(vceqq_u32(a, b)) == u32::MAX }
	}
}
