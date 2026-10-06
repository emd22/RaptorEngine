use raptor_math::Vec3f;

const MAX_WALK_SPEED: f32 = 3.8;
const MAX_SPRINT_SPEED: f32 = 5.0;
const MOVEMENT_LERP_SPEED: f32 = 10.0;
const BOB_REVERSE_LIMIT: f64 = 1.57079632679489661923;
const BOB_SPEED: f32 = 2.3;

pub const SPRINT_FOV: f32 = 85.0;
pub const WALKING_FOV: f32 = 80.0;

fn smooth_interpolate(a: f32, b: f32, speed: f32, delta_time: f32) -> f32
{
	a + (b - a) * (1.0 - (-speed * delta_time).exp())
}

/// The player's walking: the velocity eased towards where the input points, and the head bob counter.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Locomotion
{
	pub user_force: [f32; 3],
	pub bob_counter: f32,
	pub bob_reverse: u32,
}

impl Locomotion
{
	/// Eases the velocity towards `offset` (right, up, forward) relative to the way the player faces,
	/// and returns it.
	pub fn step(
		&mut self,
		delta_time: f64,
		direction: [f32; 3],
		offset: [f32; 3],
		sprinting: bool,
		speed_multiplier: f32,
	) -> [f32; 3]
	{
		let direction = Vec3f::from_array(direction);
		let up = Vec3f::new(0.0, 1.0, 0.0);

		let forward = direction * offset[2];
		let right = direction.cross(&up) * -offset[0];
		let lift = up * offset[1];

		let mut goal = forward + right + lift;

		if goal.length() > 1e-3 {
			goal.normalize_ip();
		}

		let speed = if sprinting {
			MAX_SPRINT_SPEED
		}
		else {
			MAX_WALK_SPEED
		};

		let wanted = goal * Vec3f::splat(speed) * Vec3f::splat(speed_multiplier);

		let step = 1.0 - (-MOVEMENT_LERP_SPEED * delta_time as f32).exp();
		let wanted = wanted.to_array();

		for (force, wanted) in self.user_force.iter_mut().zip(wanted) {
			*force += (wanted - *force) * step;
		}

		if goal.length() <= 0.25 {
			self.bob_counter = smooth_interpolate(self.bob_counter, 0.0, 10.0, delta_time as f32);
		}

		self.user_force
	}

	pub fn is_released(&self) -> bool
	{
		Vec3f::from_array(self.user_force).is_near_zero(0.1)
	}

	/// The head's offset from the bob counter, as `(x, y)`, scaled by the strength on each axis.
	pub fn head_bob(&self, strength_x: f32, strength_y: f32) -> (f32, f32)
	{
		let (sin, cos) = (self.bob_counter + std::f32::consts::FRAC_PI_2).sin_cos();

		(strength_x * cos, strength_y * sin)
	}

	/// Advances the head bob while the player walks on the ground.
	pub fn bob(&mut self, delta_time: f64)
	{
		let body_speed = Vec3f::from_array(self.user_force).length();
		let counter_speed = if self.bob_reverse != 0 {
			-BOB_SPEED
		}
		else {
			BOB_SPEED
		};

		self.bob_counter =
			(f64::from(self.bob_counter) + delta_time * f64::from(counter_speed) * f64::from(body_speed)) as f32;

		if f64::from(self.bob_counter) > BOB_REVERSE_LIMIT {
			self.bob_reverse = 1;
		}
		else if f64::from(self.bob_counter) < -BOB_REVERSE_LIMIT {
			self.bob_reverse = 0;
		}
	}
}

/// Opens the field of view while sprinting and closes it while walking, only when moving.
pub fn fov_step(fov: f32, sprinting: bool, moving: bool, delta_time: f64) -> f32
{
	if !moving {
		return fov;
	}

	if sprinting && fov < SPRINT_FOV {
		smooth_interpolate(fov, SPRINT_FOV, 8.0, delta_time as f32)
	}
	else if !sprinting && fov > WALKING_FOV {
		smooth_interpolate(fov, WALKING_FOV, 13.0, delta_time as f32)
	}
	else {
		fov
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn velocity_eases_towards_the_input_and_stops()
	{
		let mut loco = Locomotion::default();

		for _ in 0..120 {
			loco.step(1.0 / 60.0, [0.0, 0.0, 1.0], [0.0, 0.0, 1.0], false, 1.0);
		}

		assert!((loco.user_force[2] - MAX_WALK_SPEED).abs() < 0.01);
		assert!(!loco.is_released());

		for _ in 0..240 {
			loco.step(1.0 / 60.0, [0.0, 0.0, 1.0], [0.0; 3], false, 1.0);
		}

		assert!(loco.is_released());
	}

	#[test]
	fn sprinting_is_faster()
	{
		let mut walk = Locomotion::default();
		let mut run = Locomotion::default();

		for _ in 0..240 {
			walk.step(1.0 / 60.0, [0.0, 0.0, 1.0], [0.0, 0.0, 1.0], false, 1.0);
			run.step(1.0 / 60.0, [0.0, 0.0, 1.0], [0.0, 0.0, 1.0], true, 1.0);
		}

		assert!(run.user_force[2] > walk.user_force[2]);
	}

	#[test]
	fn the_bob_reverses_at_the_ends()
	{
		let mut loco = Locomotion {
			user_force: [0.0, 0.0, 4.0],
			..Locomotion::default()
		};

		let mut reversed = false;

		for _ in 0..600 {
			loco.bob(1.0 / 60.0);
			reversed |= loco.bob_reverse != 0;
			assert!(loco.bob_counter.abs() < 2.0);
		}

		assert!(reversed);
	}

	#[test]
	fn the_head_sits_at_the_top_of_the_bob_when_the_counter_is_zero()
	{
		let loco = Locomotion::default();
		let (x, y) = loco.head_bob(0.011, 0.018);

		assert!(x.abs() < 1e-9);
		assert!((y - 0.018).abs() < 1e-7);
	}

	#[test]
	fn the_fov_settles_on_the_right_value()
	{
		let mut fov = 80.0;

		for _ in 0..300 {
			fov = fov_step(fov, true, true, 1.0 / 60.0);
		}

		assert!((fov - SPRINT_FOV).abs() < 0.01);

		for _ in 0..300 {
			fov = fov_step(fov, false, true, 1.0 / 60.0);
		}

		assert!((fov - WALKING_FOV).abs() < 0.01);
		assert_eq!(fov_step(100.0, false, false, 1.0 / 60.0), 100.0);
	}
}
