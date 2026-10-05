/*
 * File:        bounds.rs
 * Author:      emd22
 * Created:     05/10/2026
 * Description: Axis aligned and oriented bounding boxes, and rays cast at them
 */

use core::ops::{Add, AddAssign};

use crate::mat4f::Mat4f;
use crate::vec3f::Vec3f;
use crate::vec4f::Vec4f;

/// A box lined up with the axes
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Aabb
{
	pub min: Vec3f,
	pub max: Vec3f,
}

impl Aabb
{
	#[inline]
	pub fn new(min: Vec3f, max: Vec3f) -> Self
	{
		Self { min, max }
	}

	#[inline]
	pub fn size(&self) -> Vec3f
	{
		self.max - self.min
	}

	/// Grows the box to hold `other` as well
	#[inline]
	pub fn add(&mut self, other: &Aabb)
	{
		self.min = other.min.min(&self.min);
		self.max = other.max.max(&self.max);
	}

	#[inline]
	pub fn offset_by(&mut self, offset: Vec3f) -> &mut Self
	{
		self.min += offset;
		self.max += offset;
		self
	}
}

impl AddAssign<&Aabb> for Aabb
{
	#[inline]
	fn add_assign(&mut self, other: &Aabb)
	{
		Aabb::add(self, other);
	}
}

impl AddAssign<Aabb> for Aabb
{
	#[inline]
	fn add_assign(&mut self, other: Aabb)
	{
		Aabb::add(self, &other);
	}
}

impl Add<&Aabb> for Aabb
{
	type Output = Aabb;

	#[inline]
	fn add(mut self, other: &Aabb) -> Aabb
	{
		Aabb::add(&mut self, other);
		self
	}
}

impl Add<Aabb> for Aabb
{
	type Output = Aabb;

	#[inline]
	fn add(mut self, other: Aabb) -> Aabb
	{
		Aabb::add(&mut self, &other);
		self
	}
}

/// A box that has been moved, turned or scaled, as its eight corners. Corner `n` has the maximum X
/// when bit 0 of `n` is set, the maximum Y when bit 1 is, and the maximum Z when bit 2 is.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Obb
{
	pub corners: [Vec3f; 8],
}

impl Obb
{
	/// The box `local_bounds` after `transform`
	pub fn from_local_bounds(local_bounds: &Aabb, transform: &Mat4f) -> Self
	{
		let mut obb = Self::default();

		for (corner, out) in obb.corners.iter_mut().enumerate() {
			let local = Vec4f::new(
				if corner & 1 != 0 { local_bounds.max.x } else { local_bounds.min.x },
				if corner & 2 != 0 { local_bounds.max.y } else { local_bounds.min.y },
				if corner & 4 != 0 { local_bounds.max.z } else { local_bounds.min.z },
				1.0,
			);

			let world = *transform * local;

			*out = Vec3f::new(world.x, world.y, world.z);
		}

		obb
	}

	/// The smallest axis aligned box that holds every corner
	pub fn world_aabb(&self) -> Aabb
	{
		let mut min = Vec3f::splat(f32::MAX);
		let mut max = Vec3f::splat(-f32::MAX);

		for corner in &self.corners {
			min = min.min(corner);
			max = max.max(corner);
		}

		Aabb { min, max }
	}
}

/// What `ray_cast` returns when the ray never meets the box
pub const RAY_MISS: f32 = -1.0;

/// Below this the ray is treated as running parallel to a pair of slab planes, rather than dividing by it
const PARALLEL_EPSILON: f32 = 1e-8;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Ray
{
	pub origin: Vec3f,
	pub direction: Vec3f,
	pub inv_direction: Vec3f,
}

impl Ray
{
	pub fn new(origin: Vec3f, direction: Vec3f) -> Self
	{
		Self {
			origin,
			direction,
			inv_direction: Vec3f::new(1.0 / direction.x, 1.0 / direction.y, 1.0 / direction.z),
		}
	}
}

/// 1.0 for a value with the sign bit clear and -1.0 for one with it set
fn sign_of(value: f32) -> f32
{
	f32::from_bits(1.0_f32.to_bits() | (value.to_bits() & 0x8000_0000))
}

/// How far along the ray it meets the box, in lengths of the ray's direction, or `RAY_MISS`.
///
/// Also gives the outward normal of the face it met, axis aligned in the box's own space and zero on
/// a miss. A ray starting inside the box gets the face it leaves through, which is the one it is
/// pointing at.
pub fn ray_cast_face(ray: &Ray, aabb: &Aabb) -> (f32, Vec3f)
{
	let origin = ray.origin.to_array();
	let direction = ray.direction.to_array();
	let inv_direction = ray.inv_direction.to_array();
	let min = aabb.min.to_array();
	let max = aabb.max.to_array();

	let mut t_enter = f32::NEG_INFINITY;
	let mut t_exit = f32::INFINITY;

	let mut enter_axis = 0;
	let mut exit_axis = 0;
	let mut bounded = false;

	for axis in 0..3 {
		if direction[axis].abs() < PARALLEL_EPSILON {
			if origin[axis] < min[axis] || origin[axis] > max[axis] {
				return (RAY_MISS, Vec3f::ZERO);
			}

			continue;
		}

		let mut t_near = (min[axis] - origin[axis]) * inv_direction[axis];
		let mut t_far = (max[axis] - origin[axis]) * inv_direction[axis];

		if t_near > t_far {
			core::mem::swap(&mut t_near, &mut t_far);
		}

		if t_near > t_enter {
			t_enter = t_near;
			enter_axis = axis;
		}

		if t_far < t_exit {
			t_exit = t_far;
			exit_axis = axis;
		}

		bounded = true;

		if t_exit < t_enter {
			return (RAY_MISS, Vec3f::ZERO);
		}
	}

	if !bounded || t_exit < 0.0 {
		return (RAY_MISS, Vec3f::ZERO);
	}

	let from_outside = t_enter > 0.0;
	let face_axis = if from_outside { enter_axis } else { exit_axis };

	let direction_sign = sign_of(direction[face_axis]);

	let mut face = [0.0; 3];
	face[face_axis] = if from_outside { -direction_sign } else { direction_sign };

	(
		if from_outside { t_enter } else { t_exit },
		Vec3f::from_array(face),
	)
}

/// How far along the ray it meets the box, or `RAY_MISS`
pub fn ray_cast(ray: &Ray, aabb: &Aabb) -> f32
{
	ray_cast_face(ray, aabb).0
}
