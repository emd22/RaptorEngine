use raptor_entity::{CameraCore, ProjectionKind};
use raptor_math::{Mat4f, Vec3f};

use crate::camera::resolve_view_to_texels;
use crate::shadow_atlas::DIRECTIONAL_SIZE;

pub const CAMERA_DISTANCE: f32 = 25.0;

pub struct ShadowDirectional {
	pub camera: CameraCore,
	pub map_size: [u32; 2],
}

impl Default for ShadowDirectional {
	fn default() -> Self {
		Self::new()
	}
}

impl ShadowDirectional {
	pub fn new() -> Self {
		let mut camera = CameraCore::new(ProjectionKind::Orthographic);

		camera.update();

		Self {
			camera,
			map_size: [DIRECTIONAL_SIZE, DIRECTIONAL_SIZE],
		}
	}

	pub fn place_camera(&mut self, target: Vec3f, sun_direction: Vec3f) {
		place_camera(
			&mut self.camera,
			self.map_size[0] as f32,
			target,
			sun_direction,
		);
	}
}

pub fn place_camera(
	camera: &mut CameraCore,
	texture_resolution: f32,
	target: Vec3f,
	sun_direction: Vec3f,
) {
	let eye = target + sun_direction * CAMERA_DISTANCE;

	let (eye, target) =
		resolve_view_to_texels(eye, target, Vec3f::UP, camera.width, texture_resolution);

	let [x, y, z] = eye.to_array();
	camera.position = [x, y, z, 0.0];
	camera.view = Mat4f::look_at(eye, target, Vec3f::UP).to_rows();
	camera.update_camera_matrix();
}

pub fn is_well_covered(camera: &CameraCore, position: Vec3f, edge_margin: f32) -> bool {
	let m = &camera.camera_matrix;

	let x = position.x * m[0] + position.y * m[4] + position.z * m[8] + m[12];
	let y = position.x * m[1] + position.y * m[5] + position.z * m[9] + m[13];
	let z = position.x * m[2] + position.y * m[6] + position.z * m[10] + m[14];

	let limit = 1.0 - edge_margin;

	x.abs() <= limit && y.abs() <= limit && (0.0..=1.0).contains(&z)
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn a_camera_placed_on_a_target_sees_it_in_the_middle() {
		let mut shadow = ShadowDirectional::new();
		let target = Vec3f::new(3.0, 1.0, -2.0);

		shadow.place_camera(target, Vec3f::new(0.3, 1.0, 0.2).normalize());

		assert!(is_well_covered(&shadow.camera, target, 0.5));
		assert!(!is_well_covered(
			&shadow.camera,
			target + Vec3f::new(500.0, 0.0, 0.0),
			0.0
		));
	}
}
