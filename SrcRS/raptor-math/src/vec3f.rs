use core::ops::{Add, Div, Mul, Sub};
use core::ops::{Deref, DerefMut};

use crate::vec3f_platform::{self as vp, all_equal};
use std::fmt;

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Float4XYZW
{
	pub x: f32,
	pub y: f32,
	pub z: f32,
	pub w: f32,
}

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct Vec3f(pub vp::FLOAT4);

impl Vec3f
{
	#[inline]
	pub fn new(x: f32, y: f32, z: f32) -> Self
	{
		Self(vp::set(x, y, z))
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

	#[inline]
	pub fn get_values(&self) -> [f32; 4]
	{
		return vp::get_values(self.0);
	}

	#[inline]
	pub fn dot(&self, b: &Self) -> f32
	{
		vp::dot(self.0, b.0)
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
	pub fn normalize(&self) -> Self
	{
		Self(vp::normalize(self.0))
	}

	#[inline]
	pub fn from_array(v: [f32; 3]) -> Self
	{
		Self::new(v[0], v[1], v[2])
	}

	#[inline]
	pub fn to_array(&self) -> [f32; 3]
	{
		[self.x, self.y, self.z]
	}
}

// Deref specializations to be able to decompose the vector into normal floating point
// components. Since Float4XYZW (the decomposed form) and the SIMD vector type are both guaranteed
// to be 16-byte aligned and 4 32-bit components, it is safe to use derefs in this way. Note the
// repr(transparent) as well!

impl Deref for Vec3f
{
	type Target = Float4XYZW;

	#[inline]
	fn deref(&self) -> &Float4XYZW
	{
		unsafe { &*(self as *const Self as *const Float4XYZW) }
	}
}

impl DerefMut for Vec3f
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

vector_binop!(Add, add, Vec3f, Vec3f, |a, b| Vec3f(vp::add(a.0, b.0)));
vector_binop!(Sub, sub, Vec3f, Vec3f, |a, b| Vec3f(vp::sub(a.0, b.0)));
vector_binop!(Mul, mul, Vec3f, Vec3f, |a, b| Vec3f(vp::mul(a.0, b.0)));
vector_binop!(Mul, mul, Vec3f, f32, |a, b| Vec3f(vp::muls(a.0, b)));
vector_binop!(Div, div, Vec3f, Vec3f, |a, b| Vec3f(vp::div(a.0, b.0)));
vector_binop!(Div, div, Vec3f, f32, |a, b| Vec3f(vp::divs(a.0, b)));

impl Default for Vec3f
{
	#[inline]
	fn default() -> Self
	{
		Self::splat(0.0)
	}
}

impl fmt::Debug for Vec3f
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		let [x, y, z, _] = self.get_values();

		f.debug_tuple("Vec3f")
			.field(&x)
			.field(&y)
			.field(&z)
			.finish()
	}
}

impl PartialEq for Vec3f
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
	fn create_vec3f()
	{
		let a = Vec3f::new(1.0, 2.0, 3.0);
		let b = Vec3f::new(2.0, 3.0, 4.0);

		let result = a + b;

		assert_eq!((result.x, result.y, result.z), (3.0, 5.0, 7.0));
	}
}
