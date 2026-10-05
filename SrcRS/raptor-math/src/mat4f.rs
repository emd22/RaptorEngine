/*
 * File:        mat4f.rs
 * Author:      emd22
 * Created:     05/10/2026
 * Description: 4x4 matrix type over the platform matrix, with the same operations as the C++ `Mat4f`
 */

use core::fmt;
use core::ops::Mul;

use crate::mat4::Quat;
use crate::mat4f_platform as mp;
use crate::vec3f::Vec3f;
use crate::vec4f::Vec4f;

/// A row-major 4x4 matrix that transforms row vectors: `v * M`. The translation is in the last row.
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct Mat4f(pub mp::FLOAT44);

impl Mat4f
{
	#[inline]
	pub fn zero() -> Self
	{
		Self(mp::zero())
	}

	#[inline]
	pub fn identity() -> Self
	{
		Self(mp::identity())
	}

	#[inline]
	pub fn from_rows(data: &[f32; 16]) -> Self
	{
		Self(mp::from_rows(data))
	}

	#[inline]
	pub fn from_columns(data: &[f32; 16]) -> Self
	{
		Self(mp::from_columns(data))
	}

	#[inline]
	pub fn to_rows(&self) -> [f32; 16]
	{
		mp::to_rows(self.0)
	}

	#[inline]
	pub fn as_translation(position: Vec3f) -> Self
	{
		Self(mp::translation(position.to_array()))
	}

	#[inline]
	pub fn as_scale(scale: Vec3f) -> Self
	{
		Self(mp::scale(scale.to_array()))
	}

	#[inline]
	pub fn as_rotation(quat: Quat) -> Self
	{
		Self(mp::rotation(quat.x, quat.y, quat.z, quat.w))
	}

	#[inline]
	pub fn as_rotation_x(radians: f32) -> Self
	{
		Self(mp::rotate_x(radians))
	}

	#[inline]
	pub fn as_rotation_y(radians: f32) -> Self
	{
		Self(mp::rotate_y(radians))
	}

	#[inline]
	pub fn as_rotation_z(radians: f32) -> Self
	{
		Self(mp::rotate_z(radians))
	}

	/// A view matrix looking from `eye` at `target`. Vectors are normalized by dividing by their
	/// length, as the C++ does, so the result matches it bit for bit.
	pub fn look_at(eye: Vec3f, target: Vec3f, up: Vec3f) -> Self
	{
		fn normalized(v: Vec3f) -> Vec3f
		{
			let length = v.length();

			Vec3f::new(v.x / length, v.y / length, v.z / length)
		}

		let forward = normalized(target - eye);
		let right = normalized(up.cross(&forward));
		let up = forward.cross(&right);

		Self::from_rows(&[
			right.x,
			up.x,
			forward.x,
			0.0,
			right.y,
			up.y,
			forward.y,
			0.0,
			right.z,
			up.z,
			forward.z,
			0.0,
			-eye.dot(&right),
			-eye.dot(&up),
			-eye.dot(&forward),
			1.0,
		])
	}

	/// A reverse-Z perspective projection. The plane arguments read backwards, as they do in the C++
	/// `LoadPerspectiveMatrix`: the distance passed as `far_plane` lands at window depth 1 and the one
	/// passed as `near_plane` at 0, so callers pass the far distance first.
	#[inline]
	pub fn perspective(yfov: f32, aspect_ratio: f32, near_plane: f32, far_plane: f32) -> Self
	{
		Self(mp::perspective(yfov, aspect_ratio, near_plane, far_plane))
	}

	#[inline]
	pub fn orthographic(width: f32, height: f32, near_plane: f32, far_plane: f32) -> Self
	{
		Self(mp::orthographic(width, height, near_plane, far_plane))
	}

	/// A row of the matrix, from 0 to 3
	#[inline]
	pub fn row(&self, index: usize) -> Vec4f
	{
		Vec4f::from_vector(self.0[index])
	}

	#[inline]
	pub fn translation(&self) -> Vec3f
	{
		Vec3f::from_vector(mp::get_translation(self.0))
	}

	#[inline]
	pub fn without_translation(&self) -> Self
	{
		Self(mp::without_translation(self.0))
	}

	/// The inverse by cofactors. A singular matrix gives non-finite values.
	///
	/// The sums of products are fused the way the C++ compiler fuses them, so the result matches the
	/// C++ `Inverse` bit for bit.
	pub fn inverse(&self) -> Self
	{
		/// `a*b - c*d + e*f`
		fn plus(a: f32, b: f32, c: f32, d: f32, e: f32, f: f32) -> f32
		{
			e.mul_add(f, a.mul_add(b, -(c * d)))
		}

		/// `-a*b + c*d - e*f`
		fn minus(a: f32, b: f32, c: f32, d: f32, e: f32, f: f32) -> f32
		{
			(-e).mul_add(f, (-a).mul_add(b, c * d))
		}

		/// `a*b - c*d`
		fn cross(a: f32, b: f32, c: f32, d: f32) -> f32
		{
			a.mul_add(b, -(c * d))
		}

		let flat = self.to_rows();
		let m = |row: usize, column: usize| flat[row * 4 + column];

		let s = [
			cross(m(0, 0), m(1, 1), m(1, 0), m(0, 1)),
			cross(m(0, 0), m(1, 2), m(1, 0), m(0, 2)),
			cross(m(0, 0), m(1, 3), m(1, 0), m(0, 3)),
			cross(m(0, 1), m(1, 2), m(1, 1), m(0, 2)),
			cross(m(0, 1), m(1, 3), m(1, 1), m(0, 3)),
			cross(m(0, 2), m(1, 3), m(1, 2), m(0, 3)),
		];

		let c = [
			cross(m(2, 0), m(3, 1), m(3, 0), m(2, 1)),
			cross(m(2, 0), m(3, 2), m(3, 0), m(2, 2)),
			cross(m(2, 0), m(3, 3), m(3, 0), m(2, 3)),
			cross(m(2, 1), m(3, 2), m(3, 1), m(2, 2)),
			cross(m(2, 1), m(3, 3), m(3, 1), m(2, 3)),
			cross(m(2, 2), m(3, 3), m(3, 2), m(2, 3)),
		];

		let mut determinant = s[0].mul_add(c[5], -(s[1] * c[4]));
		determinant = s[2].mul_add(c[3], determinant);
		determinant = s[3].mul_add(c[2], determinant);
		determinant = (-s[4]).mul_add(c[1], determinant);
		determinant = s[5].mul_add(c[0], determinant);

		let idet = 1.0 / determinant;

		Self::from_rows(&[
			plus(m(1, 1), c[5], m(1, 2), c[4], m(1, 3), c[3]) * idet,
			minus(m(0, 1), c[5], m(0, 2), c[4], m(0, 3), c[3]) * idet,
			plus(m(3, 1), s[5], m(3, 2), s[4], m(3, 3), s[3]) * idet,
			minus(m(2, 1), s[5], m(2, 2), s[4], m(2, 3), s[3]) * idet,
			minus(m(1, 0), c[5], m(1, 2), c[2], m(1, 3), c[1]) * idet,
			plus(m(0, 0), c[5], m(0, 2), c[2], m(0, 3), c[1]) * idet,
			minus(m(3, 0), s[5], m(3, 2), s[2], m(3, 3), s[1]) * idet,
			plus(m(2, 0), s[5], m(2, 2), s[2], m(2, 3), s[1]) * idet,
			plus(m(1, 0), c[4], m(1, 1), c[2], m(1, 3), c[0]) * idet,
			minus(m(0, 0), c[4], m(0, 1), c[2], m(0, 3), c[0]) * idet,
			plus(m(3, 0), s[4], m(3, 1), s[2], m(3, 3), s[0]) * idet,
			minus(m(2, 0), s[4], m(2, 1), s[2], m(2, 3), s[0]) * idet,
			minus(m(1, 0), c[3], m(1, 1), c[1], m(1, 2), c[0]) * idet,
			plus(m(0, 0), c[3], m(0, 1), c[1], m(0, 2), c[0]) * idet,
			minus(m(3, 0), s[3], m(3, 1), s[1], m(3, 2), s[0]) * idet,
			plus(m(2, 0), s[3], m(2, 1), s[1], m(2, 2), s[0]) * idet,
		])
	}

	#[inline]
	pub fn transposed(&self) -> Self
	{
		Self(mp::transpose(self.0))
	}

	/// Transposes the upper 3x3 block, and leaves the last row zero
	#[inline]
	pub fn transpose_mat3(&self) -> Self
	{
		Self(mp::transpose_mat3(self.0))
	}

	/// The first twelve values of the matrix
	#[inline]
	pub fn copy_as_mat3(&self) -> [f32; 12]
	{
		mp::copy_as_mat3(self.0)
	}
}

impl Default for Mat4f
{
	#[inline]
	fn default() -> Self
	{
		Self::zero()
	}
}

impl PartialEq for Mat4f
{
	fn eq(&self, other: &Self) -> bool
	{
		self.to_rows() == other.to_rows()
	}
}

impl Mul<Mat4f> for Mat4f
{
	type Output = Mat4f;

	/// `self * other`, which applies `self` first when transforming row vectors
	#[inline]
	fn mul(self, other: Mat4f) -> Mat4f
	{
		Mat4f(mp::mul(self.0, other.0))
	}
}

impl Mul<Vec4f> for Mat4f
{
	type Output = Vec4f;

	/// Transforms a row vector, `v * M`
	#[inline]
	fn mul(self, v: Vec4f) -> Vec4f
	{
		Vec4f::from_vector(mp::mul_vec4(self.0, v.0))
	}
}

impl fmt::Debug for Mat4f
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		let rows = self.to_rows();

		f.debug_list().entries(rows.as_chunks::<4>().0).finish()
	}
}
