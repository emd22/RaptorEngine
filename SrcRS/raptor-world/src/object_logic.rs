use raptor_math::bounds::{RAY_MISS, ray_cast_face};
use raptor_math::{Aabb, Mat4f, Ray, Vec3f, Vec4f};

pub struct CullInputs
{
	pub cullable: bool,
	pub has_mesh: bool,
	pub world_layer: bool,
	pub skinned: bool,
	pub instance_slots_in_use: u32,
	pub is_instance: bool,
	pub physics_enabled: bool,
}

pub fn can_be_frustum_culled(inputs: &CullInputs, bounds: &Aabb) -> bool
{
	if !inputs.cullable || !inputs.has_mesh || !inputs.world_layer {
		return false;
	}

	if inputs.skinned || inputs.instance_slots_in_use > 0 || inputs.is_instance {
		return false;
	}

	if inputs.physics_enabled {
		return false;
	}

	bounds.max.x > bounds.min.x || bounds.max.y > bounds.min.y || bounds.max.z > bounds.min.z
}

fn to_local(world_matrix: &Mat4f, point: Vec3f, w: f32) -> Vec3f
{
	let local = world_matrix.inverse() * Vec4f::new(point.x, point.y, point.z, w);

	Vec3f::new(local.x, local.y, local.z)
}

pub fn contains_point(bounds: &Aabb, world_matrix: &Mat4f, point: Vec3f) -> bool
{
	let local = to_local(world_matrix, point, 1.0);

	local.x >= bounds.min.x
		&& local.x <= bounds.max.x
		&& local.y >= bounds.min.y
		&& local.y <= bounds.max.y
		&& local.z >= bounds.min.z
		&& local.z <= bounds.max.z
}

/// The distance along a world space ray to the object's bounds, tested in the object's own space so
/// a rotated box is the box it really is, and the face it meets in that space. A miss gives
/// `RAY_MISS`.
pub fn raycast_bounds(
	bounds: &Aabb,
	world_matrix: &Mat4f,
	origin: Vec3f,
	direction: Vec3f,
) -> (f32, Vec3f)
{
	let inverse = world_matrix.inverse();

	let local_origin = inverse * Vec4f::new(origin.x, origin.y, origin.z, 1.0);
	let local_direction = inverse * Vec4f::new(direction.x, direction.y, direction.z, 0.0);

	let ray = Ray::new(
		Vec3f::new(local_origin.x, local_origin.y, local_origin.z),
		Vec3f::new(local_direction.x, local_direction.y, local_direction.z),
	);

	let (distance, face) = ray_cast_face(&ray, bounds);

	if distance == RAY_MISS {
		(RAY_MISS, Vec3f::ZERO)
	} else {
		(distance, face)
	}
}

/// The bounds of a parent after the bounds of a child that sits under it are taken in: the corners
/// of the child's box in the world, brought into the parent's own space.
pub fn merge_child_bounds(
	parent: &Aabb,
	parent_world: &Mat4f,
	child: &Aabb,
	child_world: &Mat4f,
) -> Aabb
{
	let to_parent = parent_world.inverse();

	let mut merged = *parent;

	for corner in 0..8 {
		let point = Vec3f::new(
			if corner & 1 == 0 {
				child.min.x
			} else {
				child.max.x
			},
			if corner & 2 == 0 {
				child.min.y
			} else {
				child.max.y
			},
			if corner & 4 == 0 {
				child.min.z
			} else {
				child.max.z
			},
		);

		let world = *child_world * Vec4f::new(point.x, point.y, point.z, 1.0);
		let local = (to_parent * world).xyz();

		merged.min = merged.min.min(&local);
		merged.max = merged.max.max(&local);
	}

	merged
}

/// How far the bounds reach along a direction, scaled by the object's scale.
pub fn direction_scale(bounds: &Aabb, scale: f32, direction: Vec3f) -> f32
{
	if direction.is_close_to(&Vec3f::ZERO, f32::EPSILON) {
		return 0.0;
	}

	let dir = direction.normalize();

	let pick = |positive: bool, max: f32, min: f32| if positive { max } else { -min };

	let extent = Vec3f::new(
		pick(dir.x >= 0.0, bounds.max.x, bounds.min.x),
		pick(dir.y >= 0.0, bounds.max.y, bounds.min.y),
		pick(dir.z >= 0.0, bounds.max.z, bounds.min.z),
	);

	dir.abs().dot(&extent) * scale
}

#[cfg(test)]
mod tests
{
	#[test]
	fn a_child_off_to_the_side_widens_the_parent_to_hold_it()
	{
		let parent = Aabb {
			min: Vec3f::new(-1.0, -1.0, -1.0),
			max: Vec3f::new(1.0, 1.0, 1.0),
		};
		let child = parent;

		let merged = merge_child_bounds(
			&parent,
			&Mat4f::identity(),
			&child,
			&Mat4f::as_translation(Vec3f::new(5.0, 0.0, 0.0)),
		);

		assert_eq!(merged.min.to_array(), [-1.0, -1.0, -1.0]);
		assert_eq!(merged.max.to_array(), [6.0, 1.0, 1.0]);
	}

	#[test]
	fn a_child_inside_the_parent_changes_nothing()
	{
		let parent = Aabb {
			min: Vec3f::new(-4.0, -4.0, -4.0),
			max: Vec3f::new(4.0, 4.0, 4.0),
		};
		let child = Aabb {
			min: Vec3f::new(-1.0, -1.0, -1.0),
			max: Vec3f::new(1.0, 1.0, 1.0),
		};

		let merged = merge_child_bounds(&parent, &Mat4f::identity(), &child, &Mat4f::identity());

		assert_eq!(merged.min.to_array(), parent.min.to_array());
		assert_eq!(merged.max.to_array(), parent.max.to_array());
	}

	use super::*;

	fn unit_box() -> Aabb
	{
		Aabb::new(Vec3f::splat(-1.0), Vec3f::splat(1.0))
	}

	fn inputs() -> CullInputs
	{
		CullInputs {
			cullable: true,
			has_mesh: true,
			world_layer: true,
			skinned: false,
			instance_slots_in_use: 0,
			is_instance: false,
			physics_enabled: false,
		}
	}

	#[test]
	fn static_meshes_with_volume_can_be_culled()
	{
		assert!(can_be_frustum_culled(&inputs(), &unit_box()));
		assert!(!can_be_frustum_culled(
			&inputs(),
			&Aabb::new(Vec3f::ZERO, Vec3f::ZERO)
		));
	}

	#[test]
	fn anything_that_moves_or_shares_slots_is_never_culled()
	{
		for change in [
			|i: &mut CullInputs| i.cullable = false,
			|i: &mut CullInputs| i.has_mesh = false,
			|i: &mut CullInputs| i.world_layer = false,
			|i: &mut CullInputs| i.skinned = true,
			|i: &mut CullInputs| i.instance_slots_in_use = 1,
			|i: &mut CullInputs| i.is_instance = true,
			|i: &mut CullInputs| i.physics_enabled = true,
		] {
			let mut input = inputs();
			change(&mut input);

			assert!(!can_be_frustum_culled(&input, &unit_box()));
		}
	}

	#[test]
	fn a_point_is_tested_in_the_objects_own_space()
	{
		let moved = Mat4f::as_translation(Vec3f::new(10.0, 0.0, 0.0));

		assert!(contains_point(
			&unit_box(),
			&moved,
			Vec3f::new(10.5, 0.0, 0.0)
		));
		assert!(!contains_point(
			&unit_box(),
			&moved,
			Vec3f::new(0.0, 0.0, 0.0)
		));
	}

	#[test]
	fn a_ray_hits_the_translated_box_and_reports_its_face()
	{
		let moved = Mat4f::as_translation(Vec3f::new(10.0, 0.0, 0.0));

		let (distance, face) =
			raycast_bounds(&unit_box(), &moved, Vec3f::ZERO, Vec3f::new(1.0, 0.0, 0.0));

		assert!((distance - 9.0).abs() < 1e-5);
		assert_eq!(face, Vec3f::new(-1.0, 0.0, 0.0));

		let (miss, _) = raycast_bounds(&unit_box(), &moved, Vec3f::ZERO, Vec3f::new(0.0, 1.0, 0.0));

		assert_eq!(miss, RAY_MISS);
	}

	#[test]
	fn the_direction_scale_is_the_reach_along_the_direction()
	{
		let bounds = Aabb::new(Vec3f::new(-1.0, -2.0, -3.0), Vec3f::new(4.0, 5.0, 6.0));

		assert!((direction_scale(&bounds, 2.0, Vec3f::new(1.0, 0.0, 0.0)) - 8.0).abs() < 1e-5);
		assert!((direction_scale(&bounds, 1.0, Vec3f::new(0.0, -1.0, 0.0)) - 2.0).abs() < 1e-5);
		assert_eq!(direction_scale(&bounds, 1.0, Vec3f::ZERO), 0.0);
	}
}
