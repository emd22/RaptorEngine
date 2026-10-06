use raptor_math::{Mat4f, Vec3f, Vec4f};

const PARALLEL_EPSILON: f32 = 1e-5;
const ZERO_DIRECTION_TOLERANCE: f32 = 0.00001;

pub fn point_to_world(matrix: &Mat4f, point: Vec3f) -> Vec3f
{
	(*matrix * Vec4f::new(point.x, point.y, point.z, 1.0)).xyz()
}

/// The unit direction in the world, or zero if the matrix flattens the direction away.
pub fn direction_to_world(matrix: &Mat4f, direction: Vec3f) -> Vec3f
{
	let world = (*matrix * Vec4f::new(direction.x, direction.y, direction.z, 0.0)).xyz();

	if world.is_near_zero(ZERO_DIRECTION_TOLERANCE) {
		Vec3f::ZERO
	} else {
		world.normalize()
	}
}

/// Where a ray meets the plane through `point` facing `normal`, or `point` if the ray never
/// reaches it.
pub fn ray_to_plane(origin: Vec3f, direction: Vec3f, point: Vec3f, normal: Vec3f) -> Vec3f
{
	let facing = normal.dot(&direction);

	if facing.abs() < PARALLEL_EPSILON {
		return point;
	}

	let distance = normal.dot(&(point - origin)) / facing;

	if distance < 0.0 {
		return point;
	}

	origin + direction * distance
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn a_ray_down_z_hits_a_plane_in_front_of_it()
	{
		let hit = ray_to_plane(
			Vec3f::ZERO,
			Vec3f::new(0.0, 0.0, 1.0),
			Vec3f::new(5.0, 5.0, 4.0),
			Vec3f::new(0.0, 0.0, 1.0),
		);

		assert_eq!(hit.to_array(), [0.0, 0.0, 4.0]);
	}

	#[test]
	fn a_parallel_or_backwards_ray_gives_the_point()
	{
		let point = Vec3f::new(1.0, 2.0, 3.0);

		let parallel = ray_to_plane(Vec3f::ZERO, Vec3f::new(1.0, 0.0, 0.0), point, Vec3f::UP);
		let behind = ray_to_plane(Vec3f::ZERO, Vec3f::new(0.0, -1.0, 0.0), point, Vec3f::UP);

		assert_eq!(parallel.to_array(), point.to_array());
		assert_eq!(behind.to_array(), point.to_array());
	}

	#[test]
	fn world_transforms_apply_the_translation_to_points_only()
	{
		let matrix = Mat4f::as_translation(Vec3f::new(1.0, 2.0, 3.0));

		assert_eq!(
			point_to_world(&matrix, Vec3f::ZERO).to_array(),
			[1.0, 2.0, 3.0]
		);
		assert_eq!(
			direction_to_world(&matrix, Vec3f::new(0.0, 2.0, 0.0)).to_array(),
			[0.0, 1.0, 0.0]
		);
	}
}
