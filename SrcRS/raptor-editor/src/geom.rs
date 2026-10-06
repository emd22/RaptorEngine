use raptor_math::{Aabb, Mat4f, Vec3f, Vec4f};

pub fn axis(v: Vec3f, index: usize) -> f32 {
	[v.x, v.y, v.z][index]
}

pub fn set_axis(v: &mut Vec3f, index: usize, value: f32) {
	match index {
		0 => v.x = value,
		1 => v.y = value,
		_ => v.z = value,
	}
}

pub fn map(v: Vec3f, f: impl Fn(f32) -> f32) -> Vec3f {
	Vec3f::new(f(v.x), f(v.y), f(v.z))
}

pub fn round_to_step(v: Vec3f, step: f32) -> Vec3f {
	map(v, |value| (value / step).round() * step)
}

pub fn floor_to_step(v: Vec3f, step: f32) -> Vec3f {
	map(v, |value| (value / step).floor() * step)
}

pub fn transform_point(matrix: &Mat4f, point: Vec3f) -> Vec3f {
	(*matrix * Vec4f::new(point.x, point.y, point.z, 1.0)).xyz()
}

pub fn transform_direction(matrix: &Mat4f, direction: Vec3f) -> Vec3f {
	(*matrix * Vec4f::new(direction.x, direction.y, direction.z, 0.0)).xyz()
}

pub fn ray_to_plane(
	origin: Vec3f,
	direction: Vec3f,
	plane_point: Vec3f,
	plane_normal: Vec3f,
) -> Vec3f {
	let denominator = direction.dot(&plane_normal);

	if denominator.abs() < 1e-6 {
		return plane_point;
	}

	let distance = (plane_point - origin).dot(&plane_normal) / denominator;

	if distance < 0.0 {
		return plane_point;
	}

	origin + direction * distance
}

pub fn ray_sphere(origin: Vec3f, direction: Vec3f, center: Vec3f, radius: f32) -> Option<f32> {
	let to_center = center - origin;
	let along = to_center.dot(&direction);

	if along < 0.0 {
		return None;
	}

	let closest_sq = to_center.dot(&to_center) - along * along;
	let radius_sq = radius * radius;

	if closest_sq > radius_sq {
		return None;
	}

	Some(along - (radius_sq - closest_sq).sqrt())
}

pub fn snap_direction(direction: Vec3f, step_degrees: f32) -> Vec3f {
	if step_degrees <= 0.0 {
		return direction;
	}

	let unit = direction.normalize();
	let step = step_degrees.to_radians();

	let yaw = (unit.x.atan2(unit.z) / step).round() * step;
	let pitch = (unit.y.clamp(-1.0, 1.0).asin() / step).round() * step;
	let horizontal = pitch.cos();

	Vec3f::new(yaw.sin() * horizontal, pitch.sin(), yaw.cos() * horizontal)
}

pub fn has_volume(bounds: &Aabb) -> bool {
	bounds.max.x > bounds.min.x || bounds.max.y > bounds.min.y || bounds.max.z > bounds.min.z
}

pub fn bounds_equal(a: &Aabb, b: &Aabb, tolerance: f32) -> bool {
	(0..3).all(|i| {
		(axis(a.min, i) - axis(b.min, i)).abs() <= tolerance
			&& (axis(a.max, i) - axis(b.max, i)).abs() <= tolerance
	})
}

pub fn dominant_axis(v: Vec3f) -> Vec3f {
	let a = v.abs();

	let sign = |value: f32| if value < 0.0 { -1.0 } else { 1.0 };

	if a.y >= a.x && a.y >= a.z {
		Vec3f::new(0.0, sign(v.y), 0.0)
	} else if a.x >= a.z {
		Vec3f::new(sign(v.x), 0.0, 0.0)
	} else {
		Vec3f::new(0.0, 0.0, sign(v.z))
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn ray_meets_the_plane_ahead_of_it() {
		let hit = ray_to_plane(
			Vec3f::new(0.0, 2.0, 0.0),
			Vec3f::new(0.0, -1.0, 0.0),
			Vec3f::new(5.0, 0.0, 5.0),
			Vec3f::UP,
		);

		assert!(hit.is_close_to(&Vec3f::new(0.0, 0.0, 0.0), 1e-5));
	}

	#[test]
	fn ray_away_from_the_plane_gives_the_plane_point() {
		let point = Vec3f::new(1.0, 0.0, 1.0);
		let hit = ray_to_plane(Vec3f::new(0.0, 2.0, 0.0), Vec3f::UP, point, Vec3f::UP);

		assert_eq!(hit, point);
	}

	#[test]
	fn sphere_hit_distance_is_the_entry_point() {
		let distance =
			ray_sphere(Vec3f::ZERO, Vec3f::FORWARD, Vec3f::new(0.0, 0.0, 5.0), 1.0).unwrap();

		assert!((distance - 4.0).abs() < 1e-5);
		assert!(ray_sphere(Vec3f::ZERO, Vec3f::FORWARD, Vec3f::new(0.0, 3.0, 5.0), 1.0).is_none());
	}

	#[test]
	fn snapping_a_direction_lands_on_the_step() {
		let snapped = snap_direction(Vec3f::new(0.1, -1.0, 0.05), 45.0);

		assert!(snapped.is_close_to(&Vec3f::new(0.0, -1.0, 0.0), 1e-4));
	}

	#[test]
	fn dominant_axis_keeps_its_sign() {
		assert_eq!(
			dominant_axis(Vec3f::new(-3.0, 1.0, 2.0)),
			Vec3f::new(-1.0, 0.0, 0.0)
		);
		assert_eq!(
			dominant_axis(Vec3f::new(0.1, -4.0, 2.0)),
			Vec3f::new(0.0, -1.0, 0.0)
		);
	}

	#[test]
	fn rounding_and_flooring_follow_the_step() {
		let v = Vec3f::new(0.26, 0.74, -0.3);

		assert!(round_to_step(v, 0.25).is_close_to(&Vec3f::new(0.25, 0.75, -0.25), 1e-5));
		assert!(floor_to_step(v, 0.25).is_close_to(&Vec3f::new(0.25, 0.5, -0.5), 1e-5));
	}
}
