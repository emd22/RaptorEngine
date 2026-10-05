/*
 * File:        x86_shim.rs
 * Author:      emd22
 * Created:     04/10/2026
 * Description: x86 SSE/AVX intrinsics on aarch64 (feature `simde`), so the *_avx.rs modules can
 * be              built and tested on an Apple Silicon machine.
 *
 *              The SSE through SSE4.1 intrinsics come from the `sse2neon` crate. It has no AVX,
 * no              FMA and no `_mm_permute_ps`, so the few of those the math code uses are filled
 * in              here from its public functions.
 */

#![allow(
	non_camel_case_types,
	clippy::undocumented_unsafe_blocks,
	unsafe_op_in_unsafe_fn
)]

#[cfg(not(target_arch = "aarch64"))]
compile_error!("the `simde` feature emulates SSE/AVX on aarch64 only");

use core::arch::aarch64::*;
use core::mem::transmute;

pub use sse2neon::*;

/// Two 128-bit halves standing in for a 256-bit vector of four doubles
#[derive(Clone, Copy, Debug)]
#[repr(C)]
pub struct __m256d(__m128d, __m128d);

#[inline(always)]
fn lanes_pd(v: __m128d) -> [f64; 2]
{
	unsafe { transmute::<__m128d, [f64; 2]>(v) }
}

#[inline(always)]
fn from_lanes_pd(lanes: [f64; 2]) -> __m128d
{
	unsafe { transmute::<[f64; 2], __m128d>(lanes) }
}

/////////////////////////////////////
// FMA and AVX single precision
/////////////////////////////////////

/// `a * b + c` with a single rounding
pub unsafe fn _mm_fmadd_ps(a: __m128, b: __m128, c: __m128) -> __m128
{
	let (a, b, c) = (
		transmute::<__m128, float32x4_t>(a),
		transmute::<__m128, float32x4_t>(b),
		transmute::<__m128, float32x4_t>(c),
	);

	transmute::<float32x4_t, __m128>(vfmaq_f32(c, a, b))
}

pub unsafe fn _mm_permute_ps<const IMM8: i32>(a: __m128) -> __m128
{
	_mm_shuffle_ps::<IMM8>(a, a)
}

pub unsafe fn _mm_shuffle_pd<const MASK: i32>(a: __m128d, b: __m128d) -> __m128d
{
	let (a, b) = (lanes_pd(a), lanes_pd(b));
	from_lanes_pd([a[(MASK & 1) as usize], b[((MASK >> 1) & 1) as usize]])
}

/////////////////////////////////////
// AVX double precision, four lanes
/////////////////////////////////////

pub unsafe fn _mm256_setzero_pd() -> __m256d
{
	__m256d(_mm_setzero_pd(), _mm_setzero_pd())
}

pub unsafe fn _mm256_set1_pd(a: f64) -> __m256d
{
	__m256d(_mm_set1_pd(a), _mm_set1_pd(a))
}

pub unsafe fn _mm256_setr_pd(a: f64, b: f64, c: f64, d: f64) -> __m256d
{
	__m256d(from_lanes_pd([a, b]), from_lanes_pd([c, d]))
}

/// `hi` becomes the upper two lanes and `lo` the lower two
pub unsafe fn _mm256_set_m128d(hi: __m128d, lo: __m128d) -> __m256d
{
	__m256d(lo, hi)
}

pub unsafe fn _mm256_castpd256_pd128(a: __m256d) -> __m128d
{
	a.0
}

pub unsafe fn _mm256_extractf128_pd<const IMM1: i32>(a: __m256d) -> __m128d
{
	if IMM1 & 1 == 0 { a.0 } else { a.1 }
}

pub unsafe fn _mm256_add_pd(a: __m256d, b: __m256d) -> __m256d
{
	__m256d(_mm_add_pd(a.0, b.0), _mm_add_pd(a.1, b.1))
}

pub unsafe fn _mm256_sub_pd(a: __m256d, b: __m256d) -> __m256d
{
	__m256d(_mm_sub_pd(a.0, b.0), _mm_sub_pd(a.1, b.1))
}

pub unsafe fn _mm256_mul_pd(a: __m256d, b: __m256d) -> __m256d
{
	__m256d(_mm_mul_pd(a.0, b.0), _mm_mul_pd(a.1, b.1))
}

pub unsafe fn _mm256_div_pd(a: __m256d, b: __m256d) -> __m256d
{
	__m256d(_mm_div_pd(a.0, b.0), _mm_div_pd(a.1, b.1))
}

/// `!a & b`
pub unsafe fn _mm256_andnot_pd(a: __m256d, b: __m256d) -> __m256d
{
	__m256d(_mm_andnot_pd(a.0, b.0), _mm_andnot_pd(a.1, b.1))
}

pub unsafe fn _mm256_blend_pd<const IMM4: i32>(a: __m256d, b: __m256d) -> __m256d
{
	let pick = |a: __m128d, b: __m128d, bits: i32| {
		let (a, b) = (lanes_pd(a), lanes_pd(b));
		from_lanes_pd([
			if bits & 1 != 0 { b[0] } else { a[0] },
			if bits & 2 != 0 { b[1] } else { a[1] },
		])
	};

	__m256d(pick(a.0, b.0, IMM4), pick(a.1, b.1, IMM4 >> 2))
}

pub unsafe fn _mm256_cvtps_pd(a: __m128) -> __m256d
{
	__m256d(_mm_cvtps_pd(a), _mm_cvtps_pd(_mm_movehl_ps(a, a)))
}

pub unsafe fn _mm256_cvtpd_ps(a: __m256d) -> __m128
{
	_mm_movelh_ps(_mm_cvtpd_ps(a.0), _mm_cvtpd_ps(a.1))
}
