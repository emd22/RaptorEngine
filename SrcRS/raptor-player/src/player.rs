use raptor_entity::CameraCore;
use raptor_math::mat4::Quat;
use raptor_math::quat_platform as q;
use raptor_math::{Mat4f, Vec3f};

use crate::locomotion::fov_step;
use crate::view_kick::{BACK, PITCH, ROLL, YAW};
use crate::{Locomotion, Recoil, ViewKick, ViewSway};

const JUMP_FORCE: f32 = 2.5;
const VIEW_MODEL_SWAY_ROLL: f32 = 1.25;
const HOLSTER_DROP: f32 = 0.35;
const HOLSTER_PITCH: f32 = 0.6;
const BOB_HORIZONTAL_SCALE: f32 = 0.19;
const BOB_VERTICAL_SCALE: f32 = 0.15;
const VELOCITY_SWAY: f32 = 0.004;
const VIEW_MODEL_BACK: f32 = 0.05;
const VIEW_MODEL_UP: f32 = 0.15;

const FLAG_FLY_MODE: u8 = 1 << 1;
const FLAG_UPDATE_DIRECTION: u8 = 1 << 2;

/// The first person player: where they are, which way they move, and the state of the view
/// motion that follows the camera. The engine reads the first fields straight out of the record,
/// so the layout is fixed: it is also described in the C header.
#[repr(C, align(16))]
pub struct PlayerState
{
	pub position: [f32; 4],
	pub movement_direction: [f32; 4],
	pub camera_offset: [f32; 4],
	pub head_bob_strength: [f32; 4],
	pub speed_multiplier: f32,
	pub jump_force: f32,
	pub sprinting: u8,
	flags: u8,
	pub holster: f32,
	pub head_bob: [f32; 2],
	pub locomotion: Locomotion,
	pub kick: ViewKick,
	pub recoil: Recoil,
	pub sway: ViewSway,
}

pub struct ViewModelPose
{
	pub position: [f32; 3],
	pub rotation: [f32; 4],
}

impl Default for PlayerState
{
	fn default() -> Self
	{
		Self {
			position: [0.0; 4],
			movement_direction: [0.0, 0.0, 1.0, 0.0],
			camera_offset: [0.0; 4],
			head_bob_strength: [0.011, 0.018, 0.0, 0.0],
			speed_multiplier: 1.0,
			jump_force: 0.0,
			sprinting: 0,
			flags: FLAG_UPDATE_DIRECTION,
			holster: 0.0,
			head_bob: [0.0; 2],
			locomotion: Locomotion::default(),
			kick: ViewKick::default(),
			recoil: Recoil::default(),
			sway: ViewSway::default(),
		}
	}
}

fn vec3(values: [f32; 4]) -> Vec3f
{
	Vec3f::new(values[0], values[1], values[2])
}

fn pad(value: Vec3f) -> [f32; 4]
{
	let [x, y, z] = value.to_array();

	[x, y, z, 0.0]
}

impl PlayerState
{
	fn has_flag(&self, flag: u8) -> bool
	{
		self.flags & flag != 0
	}

	fn set_flag(&mut self, flag: u8, value: bool)
	{
		if value {
			self.flags |= flag;
		} else {
			self.flags &= !flag;
		}
	}

	pub fn is_fly_mode(&self) -> bool
	{
		self.has_flag(FLAG_FLY_MODE)
	}

	pub fn set_fly_mode(&mut self, value: bool)
	{
		self.set_flag(FLAG_FLY_MODE, value);
		self.set_flag(FLAG_UPDATE_DIRECTION, true);
	}

	pub fn require_direction_update(&mut self)
	{
		self.set_flag(FLAG_UPDATE_DIRECTION, true);
	}

	pub fn start_sway_at(&mut self, camera: &CameraCore)
	{
		self.sway.prev_yaw = camera.angle_x;
		self.sway.prev_pitch = camera.angle_y;
		self.sway.yaw = 0.0;
		self.sway.pitch = 0.0;
	}

	pub fn rotate_camera(&mut self, camera: &mut CameraCore, yaw: f32, pitch: f32)
	{
		camera.rotate(yaw, pitch);
		self.require_direction_update();
	}

	pub fn rotate_head(&mut self, camera: &mut CameraCore, yaw: f32, pitch: f32)
	{
		self.recoil.cancel(yaw, pitch);
		self.rotate_camera(camera, yaw, pitch);
	}

	pub fn jump(&mut self, grounded: bool)
	{
		if grounded && !self.is_fly_mode() {
			self.jump_force = JUMP_FORCE;
		}
	}

	pub fn move_by(&mut self, by: Vec3f)
	{
		self.position = pad(vec3(self.position) + by);
	}

	/// The force to push the body with this frame: the walking velocity, with the jump added in
	/// unless flying.
	pub fn movement_force(&mut self, delta_time: f64, input: [f32; 3]) -> [f32; 3]
	{
		let direction = [
			self.movement_direction[0],
			self.movement_direction[1],
			self.movement_direction[2],
		];

		let mut force = self.locomotion.step(
			delta_time,
			direction,
			input,
			self.sprinting != 0,
			self.speed_multiplier,
		);

		if !self.is_fly_mode() {
			force[1] = self.jump_force;
			self.jump_force = 0.0;
		}

		force
	}

	fn update_direction(&mut self, camera: &CameraCore)
	{
		if !self.has_flag(FLAG_UPDATE_DIRECTION) {
			return;
		}

		let (sin_x, cos_x) = camera.angle_x.sin_cos();

		self.movement_direction = [sin_x, 0.0, cos_x, 0.0];

		if self.is_fly_mode() {
			self.movement_direction[1] = camera.angle_y.sin();
		}

		self.set_flag(FLAG_UPDATE_DIRECTION, false);
	}

	fn update_recoil(&mut self, camera: &mut CameraCore, delta_time: f32)
	{
		let (delta_yaw, delta_pitch) = self.recoil.update(delta_time);

		if delta_pitch != 0.0 || delta_yaw != 0.0 {
			let pitch_before = camera.angle_y;

			self.rotate_camera(camera, delta_yaw, delta_pitch);
			self.recoil
				.pitch_clamped(delta_pitch, camera.angle_y - pitch_before);
		}
	}

	/// Advances the camera and the view motion for a frame, with the body's position already in
	/// `position`.
	pub fn update(
		&mut self,
		camera: &mut CameraCore,
		delta_time: f64,
		grounded: bool,
		head_bob_enabled: bool,
	)
	{
		self.update_recoil(camera, delta_time as f32);
		self.update_direction(camera);

		camera.move_to(vec3(self.position) + vec3(self.camera_offset));

		let released = self.locomotion.is_released();

		self.kick.update(delta_time as f32);

		if head_bob_enabled && grounded {
			self.locomotion.bob(delta_time);
		}

		let (bob_x, bob_y) = self
			.locomotion
			.head_bob(self.head_bob_strength[0], self.head_bob_strength[1]);

		self.head_bob = [bob_x, bob_y];

		let forward = vec3(camera.direction);
		let right = Vec3f::UP.cross(&forward).normalize();
		let up = forward.cross(&right).normalize();

		camera.move_by(up * bob_y + right * bob_x);

		let fov = camera.fov_rad.to_degrees();
		let next_fov = fov_step(fov, self.sprinting != 0, !released, delta_time);

		if next_fov != fov {
			camera.fov_rad = next_fov.to_radians();
			camera.update_projection = 1;
		}

		camera.update();
	}

	/// Where the view model sits: on the camera, lagging behind its turns, bobbing with the head
	/// and kicking when fired.
	pub fn view_model_pose(
		&mut self,
		camera: &CameraCore,
		velocity: Vec3f,
		delta_time: f32,
	) -> ViewModelPose
	{
		self.sway.update(camera.angle_x, camera.angle_y, delta_time);

		let sway = q::from_euler_angles(
			Vec3f::new(
				-self.sway.pitch + self.holster * HOLSTER_PITCH,
				self.sway.yaw,
				self.sway.yaw * VIEW_MODEL_SWAY_ROLL,
			)
			.0,
		);

		let camera_rotation =
			q::from_euler_angles(Vec3f::new(-camera.angle_y, camera.angle_x, 0.0).0);
		let rotation = q::mul(camera_rotation, sway);

		let [x, y, z, w] = q::get_values(rotation);
		let basis = Mat4f::as_rotation(Quat { x, y, z, w });

		let right = basis.row(0).xyz();
		let up = basis.row(1).xyz();
		let forward = basis.row(2).xyz();

		let bob = Vec3f::new(-self.head_bob[0], -self.head_bob[1], 0.0);
		let view_model_bob = right * (bob.x * BOB_HORIZONTAL_SCALE)
			+ up * (bob.y * BOB_VERTICAL_SCALE)
			+ velocity * -VELOCITY_SWAY;

		let position = vec3(camera.position) - forward * (VIEW_MODEL_BACK + self.kick.value[BACK])
			+ up * (VIEW_MODEL_UP - self.holster * HOLSTER_DROP)
			+ view_model_bob;

		let kick = q::from_euler_angles(
			Vec3f::new(
				-self.kick.value[PITCH],
				self.kick.value[YAW],
				self.kick.value[ROLL],
			)
			.0,
		);

		ViewModelPose {
			position: position.to_array(),
			rotation: q::get_values(q::mul(rotation, kick)),
		}
	}
}

#[cfg(test)]
mod tests
{
	use raptor_entity::ProjectionKind;

	use super::*;

	fn camera() -> CameraCore
	{
		CameraCore::new(ProjectionKind::Perspective)
	}

	#[test]
	fn jumping_needs_the_ground_and_no_flying()
	{
		let mut player = PlayerState::default();

		player.jump(false);

		assert_eq!(player.jump_force, 0.0);

		player.jump(true);

		assert_eq!(player.jump_force, JUMP_FORCE);

		let mut flyer = PlayerState::default();
		flyer.set_fly_mode(true);
		flyer.jump(true);

		assert_eq!(flyer.jump_force, 0.0);
	}

	#[test]
	fn the_jump_is_spent_by_the_next_movement()
	{
		let mut player = PlayerState::default();

		player.jump(true);

		let force = player.movement_force(0.016, [0.0; 3]);

		assert_eq!(force[1], JUMP_FORCE);
		assert_eq!(player.jump_force, 0.0);
	}

	#[test]
	fn facing_follows_the_yaw_and_ignores_the_pitch_unless_flying()
	{
		let mut player = PlayerState::default();
		let mut camera = camera();

		player.rotate_camera(&mut camera, std::f32::consts::FRAC_PI_2, 0.4);
		player.update_direction(&camera);

		assert!((player.movement_direction[0] - 1.0).abs() < 1e-6);
		assert_eq!(player.movement_direction[1], 0.0);

		player.set_fly_mode(true);
		player.update_direction(&camera);

		assert!((player.movement_direction[1] - 0.4f32.sin()).abs() < 1e-6);
	}

	#[test]
	fn an_update_puts_the_camera_at_the_eye_and_clears_its_dirt()
	{
		let mut player = PlayerState::default();
		let mut camera = camera();

		player.head_bob_strength = [0.0; 4];
		player.position = [1.0, 2.0, 3.0, 0.0];
		player.camera_offset = [0.0, 0.5, 0.0, 0.0];
		player.update(&mut camera, 0.016, true, false);

		assert_eq!(camera.update_transform, 0);
		assert_eq!(camera.position[0], 1.0);
		assert_eq!(camera.position[1], 2.5);
	}

	#[test]
	fn the_view_model_sits_ahead_of_and_below_the_eye_when_still()
	{
		let mut player = PlayerState::default();
		let mut camera = camera();

		player.head_bob_strength = [0.0; 4];
		player.update(&mut camera, 0.016, true, false);

		let pose = player.view_model_pose(&camera, Vec3f::ZERO, 0.016);

		assert!((pose.position[2] + VIEW_MODEL_BACK).abs() < 1e-5);
		assert!((pose.position[1] - VIEW_MODEL_UP).abs() < 1e-5);
	}

	#[test]
	fn holstering_drops_the_view_model()
	{
		let mut player = PlayerState::default();
		let mut camera = camera();

		player.update(&mut camera, 0.016, true, false);

		let raised = player.view_model_pose(&camera, Vec3f::ZERO, 0.016).position[1];

		player.holster = 1.0;

		let lowered = player.view_model_pose(&camera, Vec3f::ZERO, 0.016).position[1];

		assert!(lowered < raised);
	}
}
