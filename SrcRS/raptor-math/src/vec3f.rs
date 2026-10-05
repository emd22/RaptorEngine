/*
 * File:        vec3f.rs
 * Author:      emd22
 * Created:     03/10/2026
 * Description: 3 component vector implementation (32-bit floating point)
 */

use core::ops::{Add, AddAssign, Div, DivAssign, Mul, MulAssign, Neg, Sub, SubAssign};
use core::ops::{Deref, DerefMut};

use crate::mat4::Quat;
use crate::vec3f_platform::{self as vp, all_equal};
use crate::vec4f::Vec4f;
use std::fmt;

/// Vector3 is stored as 4 values for alignment
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
	pub const ZERO: Self = Self::from_lanes([0.0, 0.0, 0.0, 0.0]);
	pub const ONE: Self = Self::from_lanes([1.0, 1.0, 1.0, 0.0]);

	pub const UP: Self = Self::from_lanes([0.0, 1.0, 0.0, 0.0]);
	pub const RIGHT: Self = Self::from_lanes([1.0, 0.0, 0.0, 0.0]);
	pub const FORWARD: Self = Self::from_lanes([0.0, 0.0, 1.0, 0.0]);

	const fn from_lanes(lanes: [f32; 4]) -> Self
	{
		// SAFETY: a four lane f32 SIMD vector has the layout of [f32; 4]
		Self(unsafe { core::mem::transmute::<[f32; 4], vp::FLOAT4>(lanes) })
	}

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

	/// Reads the first three values of a slice, treating any that are missing as zero
	#[inline]
	pub fn from_slice(values: &[f32]) -> Self
	{
		Self(vp::set_w(vp::load(&values[..values.len().min(3)]), 0.0))
	}

	/// Writes the three components to the start of `output`
	#[inline]
	pub fn copy_to(&self, output: &mut [f32])
	{
		output[..3].copy_from_slice(&self.to_array());
	}

	#[inline]
	pub fn set(&mut self, x: f32, y: f32, z: f32)
	{
		*self = Self::new(x, y, z);
	}

	#[inline]
	pub fn from_difference(a: &[f32], b: &[f32]) -> Self
	{
		Self::new(a[0] - b[0], a[1] - b[1], a[2] - b[2])
	}

	/// Widens to a `Vec4f` with the given W
	#[inline]
	pub fn extend(&self, w: f32) -> Vec4f
	{
		Vec4f::from_vector(vp::set_w(self.0, w))
	}

	/// The surface normal of the triangle `a, b, c`, with the winding the C++ engine uses
	pub fn surface_normal(a: &Self, b: &Self, c: &Self) -> Self
	{
		let edge1 = *b - *a;
		let edge2 = *c - *b;

		Self(vp::flip_signs(
			vp::cross(edge2.0, edge1.0),
			[true, true, true, false],
		))
		.normalize()
	}

	#[inline]
	pub fn cross(&self, b: &Self) -> Self
	{
		Self(vp::cross(self.0, b.0))
	}

	/// Rotates the vector by a unit quaternion, `q * v * conj(q)`
	pub fn rotate(&self, rotation: &Quat) -> Self
	{
		let q = Self::new(rotation.x, rotation.y, rotation.z);

		let t = q.cross(self) * 2.0;

		*self + t * rotation.w + q.cross(&t)
	}

	/// `self * b + accum`, fused
	#[inline]
	pub fn mul_add(&self, b: &Self, accum: &Self) -> Self
	{
		Self(vp::mul_add(self.0, b.0, accum.0))
	}

	#[inline]
	pub fn distance_to(&self, other: &Self) -> f32
	{
		(*other - *self).length()
	}

	#[inline]
	pub fn intersects_sphere(&self, sphere_center: &Self, sphere_radius: f32) -> bool
	{
		let diff = *self - *sphere_center;

		diff.dot(&diff) <= sphere_radius * sphere_radius
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

	/// Normalizes in place, returning the modified vector
	#[inline]
	pub fn normalize_ip(&mut self) -> &mut Self
	{
		*self = self.normalize();
		self
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

	/// Moves toward `dest` by the fraction `step`, in place
	#[inline]
	pub fn lerp_ip(&mut self, dest: &Self, step: f32) -> &mut Self
	{
		self.0 = vp::lerp(self.0, dest.0, step);
		self
	}

	/// Frame-rate independent exponential approach to `dest`
	#[inline]
	pub fn smooth_interpolate(&mut self, dest: &Self, speed: f32, delta_time: f32) -> &mut Self
	{
		self.lerp_ip(dest, 1.0 - (-speed * delta_time).exp())
	}

	/// Flips the sign of each component whose flag is false
	#[inline]
	pub fn flip_signs(&self, keep: [bool; 3]) -> Self
	{
		Self(vp::flip_signs(self.0, [keep[0], keep[1], keep[2], true]))
	}
}

impl From<Vec4f> for Vec3f
{
	/// Drops W
	#[inline]
	fn from(v: Vec4f) -> Self
	{
		Self(vp::set_w(v.0, 0.0))
	}
}

impl From<[f32; 3]> for Vec3f
{
	#[inline]
	fn from(v: [f32; 3]) -> Self
	{
		Self::from_array(v)
	}
}

impl From<Vec3f> for [f32; 3]
{
	#[inline]
	fn from(v: Vec3f) -> Self
	{
		v.to_array()
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
vector_binop!(Div, div, Vec3f, Vec3f, |a, b| Vec3f(vp::set_w(
	vp::div(a.0, b.0),
	0.0
)));
vector_binop!(Div, div, Vec3f, f32, |a, b| Vec3f(vp::set_w(
	vp::divs(a.0, b),
	0.0
)));

vector_assign_op!(AddAssign, add_assign, +, Vec3f, Vec3f);
vector_assign_op!(SubAssign, sub_assign, -, Vec3f, Vec3f);
vector_assign_op!(MulAssign, mul_assign, *, Vec3f, Vec3f);
vector_assign_op!(MulAssign, mul_assign, *, Vec3f, f32);
vector_assign_op!(DivAssign, div_assign, /, Vec3f, Vec3f);
vector_assign_op!(DivAssign, div_assign, /, Vec3f, f32);

impl Neg for Vec3f
{
	type Output = Vec3f;

	#[inline]
	fn neg(self) -> Vec3f
	{
		Vec3f(vp::neg(self.0))
	}
}

impl fmt::Display for Vec3f
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		write!(f, "({:.04}, {:.04}, {:.04})", self.x, self.y, self.z)
	}
}

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
	use crate::mat4;

	fn close(a: Vec3f, b: [f32; 3]) -> bool
	{
		a.is_close_to(&Vec3f::from_array(b), 1e-5)
	}

	#[test]
	fn create_vec3f()
	{
		let a = Vec3f::new(1.0, 2.0, 3.0);
		let b = Vec3f::new(2.0, 3.0, 4.0);

		let result = a + b;

		assert_eq!((result.x, result.y, result.z), (3.0, 5.0, 7.0));
	}

	#[test]
	fn constants_keep_w_zero()
	{
		assert_eq!(Vec3f::ONE.get_values(), [1.0, 1.0, 1.0, 0.0]);
		assert_eq!(Vec3f::UP.to_array(), [0.0, 1.0, 0.0]);
		assert_eq!(Vec3f::ZERO, Vec3f::splat(0.0));
	}

	#[test]
	fn cross_follows_the_right_hand_rule_formula()
	{
		let x = Vec3f::RIGHT;
		let y = Vec3f::UP;

		assert_eq!(x.cross(&y), Vec3f::FORWARD);
		assert_eq!(y.cross(&x), -Vec3f::FORWARD);

		let a = Vec3f::new(1.0, 2.0, 3.0);
		let b = Vec3f::new(-4.0, 5.0, 0.5);

		let c = a.cross(&b);

		assert_eq!(
			c.to_array(),
			[
				a.y * b.z - b.y * a.z,
				a.z * b.x - b.z * a.x,
				a.x * b.y - b.x * a.y
			]
		);
		assert_eq!(c.w, 0.0);
	}

	#[test]
	fn zero_and_closeness_tests_are_not_inverted()
	{
		assert!(Vec3f::ZERO.is_zero());
		assert!(!Vec3f::new(0.0, 0.0, 0.1).is_zero());
		assert!(!Vec3f::new(1.0, 2.0, 3.0).is_zero());

		assert!(Vec3f::new(0.0, 0.000001, 0.0).is_near_zero(0.0001));
		assert!(!Vec3f::new(0.0, 0.01, 0.0).is_near_zero(0.0001));

		let a = Vec3f::new(1.0, 2.0, 3.0);

		assert!(a.is_close_to(&Vec3f::new(1.0, 2.0, 3.0005), 0.001));
		assert!(!a.is_close_to(&Vec3f::new(1.0, 2.0, 3.01), 0.001));
	}

	#[test]
	fn dot_and_division_ignore_the_w_lane()
	{
		let quotient = Vec3f::new(1.0, 2.0, 3.0) / Vec3f::new(1.0, 2.0, 3.0);
		assert_eq!(quotient.get_values(), [1.0, 1.0, 1.0, 0.0]);

		let scaled = Vec3f::new(1.0, 2.0, 3.0) / 0.0;
		assert_eq!(scaled.w, 0.0);

		let mut tainted = Vec3f::new(1.0, 2.0, 3.0);
		tainted.0 = vp::set_w(tainted.0, f32::NAN);

		assert_eq!(tainted.dot(&Vec3f::ONE), 6.0);
		assert_eq!(tainted.length_sq(), 14.0);
	}

	#[test]
	fn normalize_and_length()
	{
		let v = Vec3f::new(3.0, 0.0, 4.0);

		assert_eq!(v.length(), 5.0);
		assert!(close(v.normalize(), [0.6, 0.0, 0.8]));

		let mut w = v;
		w.normalize_ip();
		assert!(close(w, [0.6, 0.0, 0.8]));
		assert_eq!(v.distance_to(&Vec3f::new(3.0, 0.0, 9.0)), 5.0);
	}

	#[test]
	fn lerp_clamp_min_max()
	{
		let a = Vec3f::new(0.0, 10.0, -4.0);
		let b = Vec3f::new(10.0, 20.0, 4.0);

		assert!(close(Vec3f::lerp(&a, &b, 0.5), [5.0, 15.0, 0.0]));

		let mut c = a;
		c.lerp_ip(&b, 0.25);
		assert!(close(c, [2.5, 12.5, -2.0]));

		let clamped = Vec3f::new(-5.0, 50.0, 1.0).clamp(&Vec3f::splat(0.0), &Vec3f::splat(10.0));
		assert_eq!(clamped.to_array(), [0.0, 10.0, 1.0]);

		assert_eq!(a.min(&b), a);
		assert_eq!(a.max(&b), b);
	}

	#[test]
	fn smooth_interpolate_approaches_the_destination()
	{
		let mut v = Vec3f::ZERO;
		let dest = Vec3f::new(1.0, 2.0, 3.0);

		v.smooth_interpolate(&dest, 10.0, 0.1);

		let step = 1.0 - (-1.0f32).exp();
		assert!(close(v, [step, 2.0 * step, 3.0 * step]));
	}

	#[test]
	fn assign_operators_and_negation()
	{
		let mut v = Vec3f::new(1.0, 2.0, 3.0);

		v += Vec3f::ONE;
		v *= 2.0;
		v -= Vec3f::new(1.0, 1.0, 1.0);
		v /= 3.0;

		assert!(close(v, [1.0, 5.0 / 3.0, 7.0 / 3.0]));
		assert_eq!((-Vec3f::new(1.0, -2.0, 3.0)).to_array(), [-1.0, 2.0, -3.0]);
	}

	#[test]
	fn flip_signs_and_abs()
	{
		let v = Vec3f::new(1.0, -2.0, 3.0);

		assert_eq!(
			v.flip_signs([true, false, false]).to_array(),
			[1.0, 2.0, -3.0]
		);
		assert_eq!(v.abs().to_array(), [1.0, 2.0, 3.0]);
	}

	#[test]
	fn slices_and_widening()
	{
		assert_eq!(
			Vec3f::from_slice(&[1.0, 2.0, 3.0, 9.0]).get_values(),
			[1.0, 2.0, 3.0, 0.0]
		);
		assert_eq!(
			Vec3f::from_slice(&[1.0, 2.0]).get_values(),
			[1.0, 2.0, 0.0, 0.0]
		);

		let mut out = [0.0; 4];
		Vec3f::new(1.0, 2.0, 3.0).copy_to(&mut out);
		assert_eq!(out, [1.0, 2.0, 3.0, 0.0]);

		let wide = Vec3f::new(1.0, 2.0, 3.0).extend(5.0);
		assert_eq!(wide.to_array(), [1.0, 2.0, 3.0, 5.0]);
		assert_eq!(Vec3f::from(wide).get_values(), [1.0, 2.0, 3.0, 0.0]);

		let d = Vec3f::from_difference(&[5.0, 5.0, 5.0], &[1.0, 2.0, 3.0]);
		assert_eq!(d.to_array(), [4.0, 3.0, 2.0]);
	}

	#[test]
	fn sphere_test_includes_the_surface()
	{
		let center = Vec3f::new(1.0, 1.0, 1.0);

		assert!(Vec3f::new(1.0, 1.0, 3.0).intersects_sphere(&center, 2.0));
		assert!(!Vec3f::new(1.0, 1.0, 3.1).intersects_sphere(&center, 2.0));
	}

	#[test]
	fn mul_add_is_a_times_b_plus_accum()
	{
		let r = Vec3f::new(1.0, 2.0, 3.0).mul_add(&Vec3f::splat(2.0), &Vec3f::ONE);

		assert_eq!(r.to_array(), [3.0, 5.0, 7.0]);
	}

	#[test]
	fn surface_normal_is_cross_of_the_second_edge_with_the_first()
	{
		let n = Vec3f::surface_normal(
			&Vec3f::new(0.0, 0.0, 0.0),
			&Vec3f::new(1.0, 0.0, 0.0),
			&Vec3f::new(1.0, 1.0, 0.0),
		);

		assert!(close(n, [0.0, 0.0, -1.0]));
	}

	#[test]
	fn rotate_matches_the_row_vector_rotation_matrix()
	{
		let axis = Vec3f::new(1.0, 2.0, -0.5).normalize();
		let half = 0.7f32;
		let (s, c) = half.sin_cos();

		let quat = mat4::Quat {
			x: axis.x * s,
			y: axis.y * s,
			z: axis.z * s,
			w: c,
		};

		let v = Vec3f::new(0.3, -1.2, 2.0);
		let rotated = v.rotate(&quat);

		let m = mat4::rotation(quat);
		let expected = [
			v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
			v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
			v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2],
		];

		assert!(close(rotated, expected));
		assert_eq!(rotated.w, 0.0);
		assert!((rotated.length() - v.length()).abs() < 1e-5);
	}
}
