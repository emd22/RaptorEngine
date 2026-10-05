use core::ops::{Add, AddAssign, Div, DivAssign, Mul, MulAssign, Neg, Sub, SubAssign};
use core::ops::{Deref, DerefMut};

use crate::vec3f::Vec3f;
use crate::vec4f_platform::{self as vp, all_equal};
use std::fmt;

pub use crate::vec3f::Float4XYZW;

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct Vec4f(pub vp::FLOAT4);

impl Vec4f
{
	pub const ZERO: Self = Self::from_lanes([0.0, 0.0, 0.0, 0.0]);
	pub const ONE: Self = Self::from_lanes([1.0, 1.0, 1.0, 1.0]);

	const fn from_lanes(lanes: [f32; 4]) -> Self
	{
		// SAFETY: a four lane f32 SIMD vector has the layout of [f32; 4]
		Self(unsafe { core::mem::transmute::<[f32; 4], vp::FLOAT4>(lanes) })
	}

	#[inline]
	pub fn new(x: f32, y: f32, z: f32, w: f32) -> Self
	{
		Self(vp::set(x, y, z, w))
	}

	#[inline]
	pub fn splat(scalar: f32) -> Self
	{
		Self(vp::splat(scalar))
	}

	#[inline]
	pub fn from_vector(v: vp::FLOAT4) -> Self
	{
		Self(v)
	}

	/// Reads the first four values of a slice, treating any that are missing as zero
	#[inline]
	pub fn from_slice(values: &[f32]) -> Self
	{
		Self(vp::load(values))
	}

	#[inline]
	pub fn from_array(v: [f32; 4]) -> Self
	{
		Self::new(v[0], v[1], v[2], v[3])
	}

	#[inline]
	pub fn to_array(&self) -> [f32; 4]
	{
		vp::get_values(self.0)
	}

	#[inline]
	pub fn get_values(&self) -> [f32; 4]
	{
		vp::get_values(self.0)
	}

	#[inline]
	pub fn set(&mut self, x: f32, y: f32, z: f32, w: f32)
	{
		*self = Self::new(x, y, z, w);
	}

	#[inline]
	pub fn set_all(&mut self, scalar: f32)
	{
		*self = Self::splat(scalar);
	}

	/// The first three components as a `Vec3f`
	#[inline]
	pub fn xyz(&self) -> Vec3f
	{
		Vec3f::from(*self)
	}

	#[inline]
	pub fn is_zero(&self) -> bool
	{
		vp::is_zero(self.0)
	}

	#[inline]
	pub fn is_near_zero(&self, tolerance: f32) -> bool
	{
		vp::is_close(self.0, vp::splat(0.0), tolerance)
	}

	#[inline]
	pub fn is_close_to(&self, other: &Self, tolerance: f32) -> bool
	{
		vp::is_close(self.0, other.0, tolerance)
	}

	#[inline]
	pub fn dot(&self, b: &Self) -> f32
	{
		vp::dot(self.0, b.0)
	}

	#[inline]
	pub fn length_sq(&self) -> f32
	{
		vp::length_sq(self.0)
	}

	#[inline]
	pub fn length(&self) -> f32
	{
		vp::length(self.0)
	}

	#[inline]
	pub fn is_normalized(&self, tolerance: f32) -> bool
	{
		(self.length_sq() - 1.0).abs() <= tolerance
	}

	#[inline]
	pub fn normalize(&self) -> Self
	{
		Self(vp::normalize(self.0))
	}

	/// Normalizes in place, returning the modified vector
	#[inline]
	pub fn normalize_ip(&mut self) -> &mut Self
	{
		*self = self.normalize();
		self
	}

	#[inline]
	pub fn abs(&self) -> Self
	{
		Self(vp::abs(self.0))
	}

	#[inline]
	pub fn min(&self, b: &Self) -> Self
	{
		Self(vp::min(self.0, b.0))
	}

	#[inline]
	pub fn max(&self, b: &Self) -> Self
	{
		Self(vp::max(self.0, b.0))
	}

	#[inline]
	pub fn clamp(&self, min: &Self, max: &Self) -> Self
	{
		Self(vp::clamp(self.0, min.0, max.0))
	}

	#[inline]
	pub fn lerp(a: &Self, b: &Self, f: f32) -> Self
	{
		Self(vp::lerp(a.0, b.0, f))
	}

	/// `self * b + accum`, fused
	#[inline]
	pub fn mul_add(&self, b: &Self, accum: &Self) -> Self
	{
		Self(vp::mul_add(self.0, b.0, accum.0))
	}

	/// Flips the sign of each component whose flag is false
	#[inline]
	pub fn flip_signs(&self, keep: [bool; 4]) -> Self
	{
		Self(vp::flip_signs(self.0, keep))
	}
}

impl From<Vec3f> for Vec4f
{
	/// Widens with W set to zero
	#[inline]
	fn from(v: Vec3f) -> Self
	{
		Self(vp::set_w(v.0, 0.0))
	}
}

impl From<[f32; 4]> for Vec4f
{
	#[inline]
	fn from(v: [f32; 4]) -> Self
	{
		Self::from_array(v)
	}
}

impl From<Vec4f> for [f32; 4]
{
	#[inline]
	fn from(v: Vec4f) -> Self
	{
		v.to_array()
	}
}

// Float4XYZW (the decomposed form) and the SIMD vector are both 16-byte aligned with four 32-bit
// lanes, and Vec4f is repr(transparent), so viewing one as the other is sound.

impl Deref for Vec4f
{
	type Target = Float4XYZW;

	#[inline]
	fn deref(&self) -> &Float4XYZW
	{
		unsafe { &*(self as *const Self as *const Float4XYZW) }
	}
}

impl DerefMut for Vec4f
{
	#[inline]
	fn deref_mut(&mut self) -> &mut Float4XYZW
	{
		unsafe { &mut *(self as *mut Self as *mut Float4XYZW) }
	}
}

/////////////////////////////////////
// Operators
/////////////////////////////////////

vector_binop!(Add, add, Vec4f, Vec4f, |a, b| Vec4f(vp::add(a.0, b.0)));
vector_binop!(Sub, sub, Vec4f, Vec4f, |a, b| Vec4f(vp::sub(a.0, b.0)));
vector_binop!(Mul, mul, Vec4f, Vec4f, |a, b| Vec4f(vp::mul(a.0, b.0)));
vector_binop!(Mul, mul, Vec4f, f32, |a, b| Vec4f(vp::muls(a.0, b)));
vector_binop!(Div, div, Vec4f, Vec4f, |a, b| Vec4f(vp::div(a.0, b.0)));
vector_binop!(Div, div, Vec4f, f32, |a, b| Vec4f(vp::div(
	a.0,
	vp::splat(b)
)));

vector_assign_op!(AddAssign, add_assign, +, Vec4f, Vec4f);
vector_assign_op!(SubAssign, sub_assign, -, Vec4f, Vec4f);
vector_assign_op!(MulAssign, mul_assign, *, Vec4f, Vec4f);
vector_assign_op!(MulAssign, mul_assign, *, Vec4f, f32);
vector_assign_op!(DivAssign, div_assign, /, Vec4f, Vec4f);
vector_assign_op!(DivAssign, div_assign, /, Vec4f, f32);

impl Neg for Vec4f
{
	type Output = Vec4f;

	#[inline]
	fn neg(self) -> Vec4f
	{
		Vec4f(vp::neg(self.0))
	}
}

impl Default for Vec4f
{
	#[inline]
	fn default() -> Self
	{
		Self::ZERO
	}
}

impl fmt::Debug for Vec4f
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		let [x, y, z, w] = self.get_values();

		f.debug_tuple("Vec4f")
			.field(&x)
			.field(&y)
			.field(&z)
			.field(&w)
			.finish()
	}
}

impl fmt::Display for Vec4f
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		write!(
			f,
			"({:.04}, {:.04}, {:.04}, {:.04})",
			self.x, self.y, self.z, self.w
		)
	}
}

impl PartialEq for Vec4f
{
	#[inline]
	fn eq(&self, other: &Self) -> bool
	{
		all_equal(self.0, other.0)
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn arithmetic_uses_all_four_lanes()
	{
		let a = Vec4f::new(1.0, 2.0, 3.0, 4.0);
		let b = Vec4f::new(2.0, 4.0, 8.0, 16.0);

		assert_eq!((a + b).to_array(), [3.0, 6.0, 11.0, 20.0]);
		assert_eq!((b - a).to_array(), [1.0, 2.0, 5.0, 12.0]);
		assert_eq!((a * b).to_array(), [2.0, 8.0, 24.0, 64.0]);
		assert_eq!((b / a).to_array(), [2.0, 2.0, 8.0 / 3.0, 4.0]);
		assert_eq!((a * 2.0).to_array(), [2.0, 4.0, 6.0, 8.0]);
		assert_eq!((b / 2.0).to_array(), [1.0, 2.0, 4.0, 8.0]);
		assert_eq!((-a).to_array(), [-1.0, -2.0, -3.0, -4.0]);
	}

	#[test]
	fn assign_operators()
	{
		let mut v = Vec4f::new(1.0, 2.0, 3.0, 4.0);

		v += Vec4f::ONE;
		v *= 2.0;
		v -= Vec4f::splat(1.0);
		v *= Vec4f::new(1.0, 0.5, 1.0, 0.5);
		v /= 2.0;

		assert_eq!(v.to_array(), [1.5, 1.25, 3.5, 2.25]);
	}

	#[test]
	fn deref_exposes_the_components()
	{
		let mut v = Vec4f::new(1.0, 2.0, 3.0, 4.0);

		assert_eq!((v.x, v.y, v.z, v.w), (1.0, 2.0, 3.0, 4.0));

		v.w = 9.0;
		assert_eq!(v.to_array(), [1.0, 2.0, 3.0, 9.0]);
	}

	#[test]
	fn length_normalize_and_dot_include_w()
	{
		let v = Vec4f::new(1.0, 2.0, 2.0, 4.0);

		assert_eq!(v.length_sq(), 25.0);
		assert_eq!(v.length(), 5.0);
		assert_eq!(v.dot(&Vec4f::ONE), 9.0);

		let n = v.normalize();
		assert!(n.is_close_to(&Vec4f::new(0.2, 0.4, 0.4, 0.8), 1e-6));
		assert!(n.is_normalized(1e-6));
		assert!(!v.is_normalized(1e-6));

		let mut m = v;
		m.normalize_ip();
		assert_eq!(m, n);
	}

	#[test]
	fn zero_and_closeness_tests_are_not_inverted()
	{
		assert!(Vec4f::ZERO.is_zero());
		assert!(!Vec4f::new(0.0, 0.0, 0.0, 1.0).is_zero());
		assert!(!Vec4f::ONE.is_zero());

		assert!(Vec4f::new(0.0, 0.0, 0.0, 0.000001).is_near_zero(0.0001));
		assert!(!Vec4f::new(0.0, 0.0, 0.0, 0.1).is_near_zero(0.0001));

		let a = Vec4f::new(1.0, 2.0, 3.0, 4.0);

		assert!(a.is_close_to(&Vec4f::new(1.0, 2.0, 3.0, 4.0005), 0.001));
		assert!(!a.is_close_to(&Vec4f::new(1.0, 2.0, 3.0, 4.1), 0.001));
	}

	#[test]
	fn flip_signs_min_max_clamp_lerp()
	{
		let v = Vec4f::new(1.0, -2.0, 3.0, -4.0);

		assert_eq!(
			v.flip_signs([true, false, true, false]).to_array(),
			[1.0, 2.0, 3.0, 4.0]
		);
		assert_eq!(v.abs().to_array(), [1.0, 2.0, 3.0, 4.0]);

		let lo = Vec4f::splat(0.0);
		let hi = Vec4f::splat(2.0);

		assert_eq!(v.clamp(&lo, &hi).to_array(), [1.0, 0.0, 2.0, 0.0]);
		assert_eq!(v.min(&lo).to_array(), [0.0, -2.0, 0.0, -4.0]);
		assert_eq!(v.max(&lo).to_array(), [1.0, 0.0, 3.0, 0.0]);
		assert_eq!(Vec4f::lerp(&lo, &hi, 0.5), Vec4f::ONE);
	}

	#[test]
	fn conversions_with_vec3f()
	{
		let wide = Vec4f::from(Vec3f::new(1.0, 2.0, 3.0));
		assert_eq!(wide.to_array(), [1.0, 2.0, 3.0, 0.0]);

		let v = Vec4f::new(1.0, 2.0, 3.0, 4.0);
		assert_eq!(v.xyz().get_values(), [1.0, 2.0, 3.0, 0.0]);

		assert_eq!(
			Vec4f::from_slice(&[1.0, 2.0]).to_array(),
			[1.0, 2.0, 0.0, 0.0]
		);
	}

	#[test]
	fn display_matches_the_cpp_formatter()
	{
		assert_eq!(
			Vec4f::new(1.0, 2.5, 3.0, 4.0).to_string(),
			"(1.0000, 2.5000, 3.0000, 4.0000)"
		);
		assert_eq!(
			Vec3f::new(1.0, 2.5, 3.0).to_string(),
			"(1.0000, 2.5000, 3.0000)"
		);
	}
}
