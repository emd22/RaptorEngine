use crate::vec3d::Vec3d;
use crate::vec3f::Vec3f;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
use core::arch::aarch64::*;
#[cfg(any(target_arch = "x86_64", feature = "simde"))]
use crate::x86::*;

#[inline]
#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
pub fn to_f64(v: Vec3f) -> Vec3d
{
	unsafe {
		Vec3d::from_vectors(
			vcvt_f64_f32(vget_low_f32(v.0)),
			vcvt_f64_f32(vget_high_f32(v.0)),
		)
	}
}

#[inline]
#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
pub fn to_f32(v: Vec3d) -> Vec3f
{
	unsafe {
		// Truncate the precision of the low and high halves of the double vector
		let a: float32x2_t = vcvt_f32_f64(v.0.0);
		let b: float32x2_t = vcvt_f32_f64(v.0.1);

		// Combine back to get float32x4_t
		Vec3f::from_vector(vcombine_f32(a, b))
	}
}

#[inline]
#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub fn to_f64(v: Vec3f) -> Vec3d
{
	unsafe { Vec3d(_mm256_cvtps_pd(v.0)) }
}

#[inline]
#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub fn to_f32(v: Vec3d) -> Vec3f
{
	unsafe { Vec3f::from_vector(_mm256_cvtpd_ps(v.0)) }
}
