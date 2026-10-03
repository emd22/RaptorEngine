use core::ops::{Add, Div, Mul, Sub};
use core::ops::{Deref, DerefMut};

use crate::vec2d_platform as vp2;
use crate::vec3d_platform as vp;

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Double4XYZW
{
	pub x: f64,
	pub y: f64,
	pub z: f64,
	pub w: f64,
}

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct Vec3d(pub vp::DOUBLE4);

impl Vec3d
{
	#[inline]
	pub fn new(x: f64, y: f64, z: f64) -> Self
	{
		Self(vp::set(x, y, z))
	}

	pub fn from_vectors(a: vp2::DOUBLE2, b: vp2::DOUBLE2) -> Self
	{
		Self(vp::set_vectors(a, b))
	}

	#[inline]
	pub fn splat(scalar: f64) -> Self
	{
		Self(vp::splat(scalar))
	}

	#[inline]
	pub fn from_array(v: [f64; 3]) -> Self
	{
		Self::new(v[0], v[1], v[2])
	}

	#[inline]
	pub fn to_array(&self) -> [f64; 3]
	{
		[self.x, self.y, self.z]
	}

	#[inline]
	pub fn dot(&self, b: &Self) -> f64
	{
		vp::dot(self.0, b.0)
	}

	#[inline]
	pub fn cross(&self, b: &Self) -> Self
	{
		Self::new(
			self.y * b.z - self.z * b.y,
			self.z * b.x - self.x * b.z,
			self.x * b.y - self.y * b.x,
		)
	}

	#[inline]
	pub fn abs(&self) -> Self
	{
		Self(vp::abs(self.0))
	}

	#[inline]
	pub fn length_sq(&self) -> f64
	{
		vp::length_sq(self.0)
	}

	#[inline]
	pub fn length(&self) -> f64
	{
		vp::length(self.0)
	}
}

// Deref specializations to be able to decompose the vector into normal floating point
// components. Since Float4XYZW (the decomposed form) and the SIMD vector type are both guaranteed
// to be 16-byte aligned and 4 32-bit components, it is safe to use derefs in this way. Note the
// repr(transparent) as well!

impl Deref for Vec3d
{
	type Target = Double4XYZW;

	#[inline]
	fn deref(&self) -> &Double4XYZW
	{
		unsafe { &*(self as *const Self as *const Double4XYZW) }
	}
}

impl DerefMut for Vec3d
{
	#[inline]
	fn deref_mut(&mut self) -> &mut Double4XYZW
	{
		unsafe { &mut *(self as *mut Self as *mut Double4XYZW) }
	}
}

/////////////////////////////////////
// Operators
/////////////////////////////////////

vector_binop!(Add, add, Vec3d, Vec3d, |a, b| Vec3d(vp::add(a.0, b.0)));
vector_binop!(Sub, sub, Vec3d, Vec3d, |a, b| Vec3d(vp::sub(a.0, b.0)));
vector_binop!(Mul, mul, Vec3d, Vec3d, |a, b| Vec3d(vp::mul(a.0, b.0)));
vector_binop!(Mul, mul, Vec3d, f64, |a, b| Vec3d(vp::muls(a.0, b)));
vector_binop!(Div, div, Vec3d, Vec3d, |a, b| Vec3d(vp::div(a.0, b.0)));
vector_binop!(Div, div, Vec3d, f64, |a, b| Vec3d(vp::divs(a.0, b)));

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn create_vec3f()
	{
		let a = Vec3d::new(1.0, 2.0, 3.0);
		let b = Vec3d::new(2.0, 3.0, 4.0);

		let result = a + b;

		assert_eq!((result.x, result.y, result.z), (3.0, 5.0, 7.0));
	}
}
