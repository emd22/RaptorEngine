use std::f32::consts::TAU;

pub const COOLDOWN: f32 = 0.25;
pub const DEFAULT_MIN_SPEED: f32 = 3.5;

const MIN_SIZE: f32 = 0.4;
const MAX_SIZE: f32 = 1.0;
const SPEED_RANGE: f32 = 8.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Splat {
	pub point: [f32; 3],
	pub size: f32,
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
	[
		a[1] * b[2] - a[2] * b[1],
		a[2] * b[0] - a[0] * b[2],
		a[0] * b[1] - a[1] * b[0],
	]
}

fn normalized(v: [f32; 3]) -> [f32; 3] {
	let length = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();

	if length <= f32::EPSILON {
		return [0.0; 3];
	}

	[v[0] / length, v[1] / length, v[2] / length]
}

pub fn splats(
	point: [f32; 3],
	normal: [f32; 3],
	speed: f32,
	min_speed: f32,
	mut random_unit: impl FnMut() -> f32,
) -> Vec<Splat> {
	let strength = ((speed - min_speed) / SPEED_RANGE).clamp(0.0, 1.0);
	let size = MIN_SIZE + (MAX_SIZE - MIN_SIZE) * strength;

	let mut result = vec![Splat { point, size }];

	let reference = if normal[1].abs() < 0.9 {
		[0.0, 1.0, 0.0]
	} else {
		[1.0, 0.0, 0.0]
	};
	let tangent = normalized(cross(reference, normal));
	let bitangent = cross(normal, tangent);

	let satellites = 2 + (strength * 2.0) as u32;

	for _ in 0..satellites {
		let angle = random_unit() * TAU;
		let distance = size * (0.4 + 0.6 * random_unit());
		let (sin, cos) = angle.sin_cos();

		let offset = [
			(tangent[0] * cos + bitangent[0] * sin) * distance,
			(tangent[1] * cos + bitangent[1] * sin) * distance,
			(tangent[2] * cos + bitangent[2] * sin) * distance,
		];

		result.push(Splat {
			point: [
				point[0] + offset[0],
				point[1] + offset[1],
				point[2] + offset[2],
			],
			size: size * (0.25 + 0.25 * random_unit()),
		});
	}

	result
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn a_slow_impact_makes_a_small_splat_with_two_satellites() {
		let result = splats([1.0, 0.0, 2.0], [0.0, 1.0, 0.0], 3.5, 3.5, || 0.5);

		assert_eq!(result.len(), 3);
		assert_eq!(result[0].size, MIN_SIZE);
	}

	#[test]
	fn a_fast_impact_makes_a_big_splat_with_four_satellites() {
		let result = splats([0.0; 3], [0.0, 1.0, 0.0], 100.0, 3.5, || 0.5);

		assert_eq!(result.len(), 5);
		assert_eq!(result[0].size, MAX_SIZE);
	}

	#[test]
	fn satellites_land_on_the_surface_plane() {
		let result = splats([0.0; 3], [0.0, 1.0, 0.0], 8.0, 3.5, || 0.3);

		for splat in &result[1..] {
			assert!(splat.point[1].abs() < 1e-5);
		}
	}
}
