/*
 * File:        frustum.rs
 * Author:      emd22
 * Created:     05/10/2026
 * Description: The volume a camera sees, and tests for what is inside it
 */

use crate::bounds::{Aabb, Obb};
use crate::mat4f::Mat4f;
use crate::vec3f::Vec3f;
use crate::vec4f::Vec4f;

/// The planes of a frustum, in the order of the C++ `eFrustumPlane`.
///
/// With the engine's projections, whose depth runs from 0 at the near distance to 1 at the far one
/// (the viewport reverses it), the plane named `Near` is the one at the far distance and `Far` is the
/// one at the near distance. The names are kept as they are in the C++ so that masks mean the same
/// thing in both; only the side planes are masked by name in the engine.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FrustumPlane
{
	Left = 0,
	Right = 1,
	Bottom = 2,
	Top = 3,
	Near = 4,
	Far = 5,
}

pub const fn plane_bit(plane: FrustumPlane) -> u32
{
	1 << plane as u32
}

pub const ALL_PLANES: u32 = 0x3F;

pub const SIDE_PLANES: u32 = plane_bit(FrustumPlane::Left)
	| plane_bit(FrustumPlane::Right)
	| plane_bit(FrustumPlane::Bottom)
	| plane_bit(FrustumPlane::Top);

fn to_plane(v: Vec4f) -> Vec4f
{
	v / Vec3f::from(v).length()
}

/// The six planes of a view volume, each as a normal pointing inwards and its distance, normalized
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Frustum
{
	planes: [Vec4f; 6],
}

fn planes_to_test(mask: u32) -> impl Iterator<Item = usize>
{
	(0..=FrustumPlane::Far as usize).filter(move |index| mask & (1 << index) != 0)
}

impl Frustum
{
	/// The frustum of `view_projection`, which has to be the reverse-Z view and projection of a
	/// camera.
	pub fn from_view_projection(view_projection: &Mat4f) -> Self
	{
		let mut frustum = Self::default();

		frustum.rebuild(view_projection);

		frustum
	}

	pub fn rebuild(&mut self, view_projection: &Mat4f)
	{
		let vp = view_projection.transposed();

		let (x, y, z, w) = (vp.row(0), vp.row(1), vp.row(2), vp.row(3));

		self.planes[FrustumPlane::Left as usize] = to_plane(w + x);
		self.planes[FrustumPlane::Right as usize] = to_plane(w - x);

		self.planes[FrustumPlane::Bottom as usize] = to_plane(w + y);
		self.planes[FrustumPlane::Top as usize] = to_plane(w - y);

		self.planes[FrustumPlane::Near as usize] = to_plane(w - z);
		self.planes[FrustumPlane::Far as usize] = to_plane(z);
	}

	pub fn plane(&self, plane: FrustumPlane) -> Vec4f
	{
		self.planes[plane as usize]
	}

	/// Whether a tile of the world could be in view. Tiles are flat on the ground, so only the left,
	/// right, near and far planes are tested.
	pub fn tile_intersects_aabb(&self, tile: &Aabb) -> bool
	{
		const TESTED: [FrustumPlane; 4] = [
			FrustumPlane::Left,
			FrustumPlane::Right,
			FrustumPlane::Near,
			FrustumPlane::Far,
		];

		for plane in TESTED {
			let plane = self.plane(plane);

			let furthest_along_the_normal = Vec3f::new(
				if plane.x >= 0.0 { tile.max.x } else { tile.min.x },
				if plane.y >= 0.0 { tile.max.y } else { tile.min.y },
				if plane.z >= 0.0 { tile.max.z } else { tile.min.z },
			);

			if Vec3f::from(plane).dot(&furthest_along_the_normal) + plane.w < 0.0 {
				return false;
			}
		}

		true
	}

	/// Whether any part of the sphere is inside the frustum. Can let through spheres that are just
	/// outside of a corner.
	pub fn intersects_sphere(&self, center: Vec3f, radius: f32, plane_mask: u32) -> bool
	{
		planes_to_test(plane_mask).all(|index| {
			let plane = self.planes[index];

			Vec3f::from(plane).dot(&center) + plane.w >= -radius
		})
	}

	pub fn intersects_aabb(&self, aabb: &Aabb, plane_mask: u32) -> bool
	{
		let center = (aabb.min + aabb.max) * 0.5;
		let half_extent = (aabb.max - aabb.min) * 0.5;

		planes_to_test(plane_mask).all(|index| {
			let plane = self.planes[index];
			let normal = Vec3f::from(plane);

			let reach = half_extent.dot(&normal.abs());

			normal.dot(&center) + plane.w >= -reach
		})
	}

	pub fn intersects_obb(&self, obb: &Obb, plane_mask: u32) -> bool
	{
		let center = (obb.corners[0] + obb.corners[7]) * 0.5;
		let half_x = (obb.corners[1] - obb.corners[0]) * 0.5;
		let half_y = (obb.corners[2] - obb.corners[0]) * 0.5;
		let half_z = (obb.corners[4] - obb.corners[0]) * 0.5;

		planes_to_test(plane_mask).all(|index| {
			let plane = self.planes[index];
			let normal = Vec3f::from(plane);

			let reach =
				half_x.dot(&normal).abs() + half_y.dot(&normal).abs() + half_z.dot(&normal).abs();

			normal.dot(&center) + plane.w >= -reach
		})
	}
}

/// The box around everything `view_projection` can see
pub fn frustum_bounding_box(view_projection: &Mat4f) -> Aabb
{
	const NDC_CORNERS: [[f32; 3]; 8] = [
		[-1.0, -1.0, 0.0],
		[1.0, -1.0, 0.0],
		[1.0, 1.0, 0.0],
		[-1.0, 1.0, 0.0],
		[-1.0, -1.0, 1.0],
		[1.0, -1.0, 1.0],
		[1.0, 1.0, 1.0],
		[-1.0, 1.0, 1.0],
	];

	let inverse = view_projection.inverse();

	let mut min = Vec3f::new(f32::MAX, f32::MAX, f32::MAX);
	let mut max = Vec3f::new(-f32::MAX, -f32::MAX, -f32::MAX);

	for corner in NDC_CORNERS {
		let mut world = inverse * Vec4f::new(corner[0], corner[1], corner[2], 1.0);
		world = world / world.w;

		let point = Vec3f::new(world.x, world.y, world.z);

		min = min.min(&point);
		max = max.max(&point);
	}

	Aabb { min, max }
}
