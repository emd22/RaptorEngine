use crate::vec3d::Vec3d;
use crate::vec3f::Vec3f;

#[cfg(target_arch = "aarch64")]
use core::arch::aarch64::*;

#[inline]
#[cfg(target_arch = "aarch64")]
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
#[cfg(target_arch = "aarch64")]
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
