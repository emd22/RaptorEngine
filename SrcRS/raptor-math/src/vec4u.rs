use core::ops::{Add, AddAssign, Mul, MulAssign, Sub, SubAssign};
use core::ops::{Deref, DerefMut};

use crate::vec4u_platform::{self as vp, all_equal};
use std::fmt;

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UInt4XYZW
{
	pub x: u32,
	pub y: u32,
	pub z: u32,
	pub w: u32,
}

/// Four lane unsigned integer vector. Arithmetic wraps on overflow, as the SIMD instructions do.
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct Vec4u(pub vp::UINT4);

impl Vec4u
{
	pub const ZERO: Self = Self::from_lanes([0, 0, 0, 0]);
	pub const ONE: Self = Self::from_lanes([1, 1, 1, 1]);

	const fn from_lanes(lanes: [u32; 4]) -> Self
	{
		// SAFETY: a four lane u32 SIMD vector has the layout of [u32; 4]
		Self(unsafe { core::mem::transmute::<[u32; 4], vp::UINT4>(lanes) })
	}

	#[inline]
	pub fn new(x: u32, y: u32, z: u32, w: u32) -> Self
	{
		Self(vp::set(x, y, z, w))
	}

	#[inline]
	pub fn splat(scalar: u32) -> Self
	{
		Self(vp::splat(scalar))
	}

	#[inline]
	pub fn from_array(v: [u32; 4]) -> Self
	{
		Self::new(v[0], v[1], v[2], v[3])
	}

	#[inline]
	pub fn to_array(&self) -> [u32; 4]
	{
		vp::get_values(self.0)
	}

	#[inline]
	pub fn set(&mut self, x: u32, y: u32, z: u32, w: u32)
	{
		*self = Self::new(x, y, z, w);
	}

	#[inline]
	pub fn set_all(&mut self, scalar: u32)
	{
		*self = Self::splat(scalar);
	}

	#[inline]
	pub fn is_zero(&self) -> bool
	{
		vp::is_zero(self.0)
	}
}

impl From<[u32; 4]> for Vec4u
{
	#[inline]
	fn from(v: [u32; 4]) -> Self
	{
		Self::from_array(v)
	}
}

impl From<Vec4u> for [u32; 4]
{
	#[inline]
	fn from(v: Vec4u) -> Self
	{
		v.to_array()
	}
}

impl Deref for Vec4u
{
	type Target = UInt4XYZW;

	#[inline]
	fn deref(&self) -> &UInt4XYZW
	{
		// SAFETY: UInt4XYZW and the SIMD vector are both 16-byte aligned with four u32 lanes, and
		// Vec4u is repr(transparent)
		unsafe { &*(self as *const Self as *const UInt4XYZW) }
	}
}

impl DerefMut for Vec4u
{
	#[inline]
	fn deref_mut(&mut self) -> &mut UInt4XYZW
	{
		// SAFETY: as for `deref`
		unsafe { &mut *(self as *mut Self as *mut UInt4XYZW) }
	}
}

vector_binop!(Add, add, Vec4u, Vec4u, |a, b| Vec4u(vp::add(a.0, b.0)));
vector_binop!(Sub, sub, Vec4u, Vec4u, |a, b| Vec4u(vp::sub(a.0, b.0)));
vector_binop!(Mul, mul, Vec4u, Vec4u, |a, b| Vec4u(vp::mul(a.0, b.0)));
vector_binop!(Mul, mul, Vec4u, u32, |a, b| Vec4u(vp::muls(a.0, b)));

vector_assign_op!(AddAssign, add_assign, +, Vec4u, Vec4u);
vector_assign_op!(SubAssign, sub_assign, -, Vec4u, Vec4u);
vector_assign_op!(MulAssign, mul_assign, *, Vec4u, Vec4u);
vector_assign_op!(MulAssign, mul_assign, *, Vec4u, u32);

impl Default for Vec4u
{
	#[inline]
	fn default() -> Self
	{
		Self::ZERO
	}
}

impl PartialEq for Vec4u
{
	#[inline]
	fn eq(&self, other: &Self) -> bool
	{
		all_equal(self.0, other.0)
	}
}

impl Eq for Vec4u {}

impl fmt::Debug for Vec4u
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		let [x, y, z, w] = self.to_array();

		f.debug_tuple("Vec4u")
			.field(&x)
			.field(&y)
			.field(&z)
			.field(&w)
			.finish()
	}
}

impl fmt::Display for Vec4u
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		write!(f, "({}, {}, {}, {})", self.x, self.y, self.z, self.w)
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn arithmetic_is_lane_wise_and_wraps()
	{
		let a = Vec4u::new(1, 2, 3, u32::MAX);
		let b = Vec4u::new(10, 20, 30, 1);

		assert_eq!((a + b).to_array(), [11, 22, 33, 0]);
		assert_eq!((b - a).to_array(), [9, 18, 27, 2]);
		assert_eq!((a * b).to_array(), [10, 40, 90, u32::MAX]);
		assert_eq!((a * 3).to_array(), [3, 6, 9, u32::MAX - 2]);
	}

	#[test]
	fn assign_operators()
	{
		let mut v = Vec4u::new(1, 2, 3, 4);

		v += Vec4u::ONE;
		v *= 2;
		v -= Vec4u::splat(1);
		v *= Vec4u::new(1, 2, 3, 4);

		assert_eq!(v.to_array(), [3, 10, 21, 36]);
	}

	#[test]
	fn zero_test_is_not_inverted()
	{
		assert!(Vec4u::ZERO.is_zero());
		assert!(!Vec4u::new(0, 0, 0, 1).is_zero());
		assert!(!Vec4u::ONE.is_zero());
	}

	#[test]
	fn equality_needs_every_lane()
	{
		assert_eq!(Vec4u::new(1, 2, 3, 4), Vec4u::from_array([1, 2, 3, 4]));
		assert_ne!(Vec4u::new(1, 2, 3, 4), Vec4u::new(1, 2, 3, 5));
		assert_ne!(Vec4u::new(1, 2, 3, 4), Vec4u::new(9, 9, 9, 4));
	}

	#[test]
	fn components_and_display()
	{
		let mut v = Vec4u::new(1, 2, 3, 4);

		v.z = 7;
		assert_eq!((v.x, v.y, v.z, v.w), (1, 2, 7, 4));
		assert_eq!(v.to_string(), "(1, 2, 7, 4)");

		v.set_all(5);
		assert_eq!(v.to_array(), [5; 4]);
	}
}
