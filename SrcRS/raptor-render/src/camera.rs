use raptor_entity::{CameraCore, ProjectionKind};
use raptor_math::Vec3f;

pub const WEAPON_FAR_PLANE: f32 = 0.01;
pub const WEAPON_NEAR_PLANE: f32 = 20.0;

pub fn perspective(fov_degrees: f32, aspect: f32, near_plane: f32, far_plane: f32) -> CameraCore
{
	let mut camera = CameraCore::new(ProjectionKind::Perspective);

	camera.z_near = near_plane;
	camera.z_far = far_plane;
	camera.fov_rad = fov_degrees.to_radians();
	camera.aspect = aspect;
	camera.update_projection = 1;
	camera.update();

	camera
}

pub fn orthographic() -> CameraCore
{
	CameraCore::new(ProjectionKind::Orthographic)
}

pub fn on_window_resize(camera: &mut CameraCore, size: (u32, u32))
{
	camera.aspect = size.0 as f32 / size.1 as f32;
	camera.update_projection = 1;
}

pub fn forward_vector(camera: &CameraCore) -> Vec3f
{
	Vec3f::new(camera.direction[0], camera.direction[1], camera.direction[2])
}

pub fn right_vector(camera: &CameraCore) -> Vec3f
{
	Vec3f::UP.cross(&forward_vector(camera)).normalize()
}

pub fn up_vector(camera: &CameraCore) -> Vec3f
{
	forward_vector(camera).cross(&right_vector(camera)).normalize()
}

/// Moves `eye` and `target` by the same offset so that the eye sits on the texel grid of a shadow
/// map `width` wide with `resolution` texels, in the plane across the view direction. The grid then
/// stays put against the world as the camera moves, so shadow edges do not shimmer.
pub fn resolve_view_to_texels(
	eye: Vec3f,
	target: Vec3f,
	world_up: Vec3f,
	width: f32,
	resolution: f32,
) -> (Vec3f, Vec3f)
{
	let forward = (target - eye).normalize();
	let right = world_up.cross(&forward).normalize();
	let up = forward.cross(&right);

	let texel_size = width / resolution;
	let texel_size_recip = 1.0 / texel_size;

	let view_x = eye.dot(&right);
	let view_y = eye.dot(&up);

	let snapped_x = (view_x * texel_size_recip).floor() * texel_size;
	let snapped_y = (view_y * texel_size_recip).floor() * texel_size;

	let offset = right * (snapped_x - view_x) + up * (snapped_y - view_y);

	(eye + offset, target + offset)
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn snapping_moves_eye_and_target_together_onto_the_texel_grid()
	{
		let eye = Vec3f::new(0.37, 0.52, -10.0);
		let target = Vec3f::new(0.37, 0.52, 0.0);

		let (snapped_eye, snapped_target) =
			resolve_view_to_texels(eye, target, Vec3f::UP, 40.0, 1000.0);

		let delta_eye = snapped_eye - eye;
		let delta_target = snapped_target - target;

		assert_eq!(delta_eye.to_array(), delta_target.to_array());

		let texel = 40.0 / 1000.0;
		let cells = snapped_eye.x / texel;

		assert!((cells - cells.round()).abs() < 1e-3);
	}
}
