const APPLY_SPEED: f32 = 40.0;
const RECOVERY_DELAY: f32 = 0.1;
const RECOVERY_RAMP: f32 = 0.08;
const SETTLE_ANGLE: f32 = 0.01 * (std::f64::consts::PI / 180.0) as f32;
const CLAMP_EPSILON: f32 = 1.0e-6;

/// Camera recoil. A shot adds pending angle, which is fed into the camera quickly. What has been fed in
/// is `offset`, the angle still owed back, and it decays after a short idle delay.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Recoil
{
	pub pending_pitch: f32,
	pub pending_yaw: f32,
	pub offset_pitch: f32,
	pub offset_yaw: f32,
	pub recovery: f32,
	pub idle_time: f32,
}

fn recover_axis(offset: &mut f32, fraction: f32) -> f32
{
	let mut back = *offset * fraction;

	if (*offset - back).abs() < SETTLE_ANGLE {
		back = *offset;
	}

	*offset -= back;

	back
}

fn cancel_axis(offset: &mut f32, input: f32)
{
	if *offset * input < 0.0 {
		*offset += input.abs().min(offset.abs()).copysign(input);
	}
}

impl Recoil
{
	pub fn add(&mut self, pitch: f32, yaw: f32)
	{
		self.pending_pitch += pitch;
		self.pending_yaw += yaw;
		self.idle_time = 0.0;
	}

	/// The player turned the camera by `yaw` and `pitch`. Turning against the recoil pays it back.
	pub fn cancel(&mut self, yaw: f32, pitch: f32)
	{
		cancel_axis(&mut self.offset_yaw, yaw);
		cancel_axis(&mut self.offset_pitch, pitch);
	}

	/// How far the camera should turn this frame, as `(yaw, pitch)`.
	pub fn update(&mut self, delta_time: f32) -> (f32, f32)
	{
		self.idle_time += delta_time;

		let blend = 1.0 - (-delta_time * APPLY_SPEED).exp();
		let step_pitch = self.pending_pitch * blend;
		let step_yaw = self.pending_yaw * blend;

		self.pending_pitch -= step_pitch;
		self.pending_yaw -= step_yaw;

		self.offset_pitch += step_pitch;
		self.offset_yaw += step_yaw;

		let mut delta_pitch = step_pitch;
		let mut delta_yaw = step_yaw;

		let recovering = self.idle_time - RECOVERY_DELAY;

		if recovering > 0.0 && self.recovery > 0.0 {
			let t = (recovering / RECOVERY_RAMP).min(1.0);
			let ease = t * t * (3.0 - 2.0 * t);
			let fraction = 1.0 - (-delta_time * self.recovery * ease).exp();

			delta_pitch -= recover_axis(&mut self.offset_pitch, fraction);
			delta_yaw -= recover_axis(&mut self.offset_yaw, fraction);
		}

		(delta_yaw, delta_pitch)
	}

	/// The camera could only turn `applied_pitch` of the `delta_pitch` asked for, as it hit its limit.
	pub fn pitch_clamped(&mut self, delta_pitch: f32, applied_pitch: f32)
	{
		let clamped = delta_pitch - applied_pitch;

		if clamped.abs() > CLAMP_EPSILON {
			self.offset_pitch -= clamped;
		}
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn run(recoil: &mut Recoil, seconds: f32) -> (f32, f32)
	{
		let steps = (seconds * 120.0) as usize;
		let (mut yaw, mut pitch) = (0.0, 0.0);

		for _ in 0..steps {
			let (dy, dp) = recoil.update(1.0 / 120.0);

			yaw += dy;
			pitch += dp;
		}

		(yaw, pitch)
	}

	#[test]
	fn a_shot_kicks_the_camera_and_is_paid_back_in_full()
	{
		let mut recoil = Recoil {
			recovery: 7.0,
			..Recoil::default()
		};

		recoil.add(0.1, 0.02);

		let (yaw, pitch) = run(&mut recoil, 3.0);

		assert!(yaw.abs() < 1e-5 && pitch.abs() < 1e-5, "{yaw} {pitch}");
		assert_eq!((recoil.offset_pitch, recoil.offset_yaw), (0.0, 0.0));
	}

	#[test]
	fn nothing_recovers_without_a_rate()
	{
		let mut recoil = Recoil::default();

		recoil.add(0.1, 0.0);

		let (_, pitch) = run(&mut recoil, 2.0);

		assert!((pitch - 0.1).abs() < 1e-5);
	}

	#[test]
	fn turning_against_the_recoil_pays_it_back_but_never_past_zero()
	{
		let mut recoil = Recoil {
			offset_pitch: 0.1,
			..Recoil::default()
		};

		recoil.cancel(0.0, -0.04);
		assert!((recoil.offset_pitch - 0.06).abs() < 1e-7);

		recoil.cancel(0.0, -1.0);
		assert_eq!(recoil.offset_pitch, 0.0);

		recoil.cancel(0.0, 0.5);
		assert_eq!(recoil.offset_pitch, 0.0);
	}

	#[test]
	fn a_clamped_camera_gives_back_the_angle_it_could_not_turn()
	{
		let mut recoil = Recoil {
			offset_pitch: 0.2,
			..Recoil::default()
		};

		recoil.pitch_clamped(0.1, 0.04);
		assert!((recoil.offset_pitch - 0.14).abs() < 1e-7);

		recoil.pitch_clamped(0.1, 0.1);
		assert!((recoil.offset_pitch - 0.14).abs() < 1e-7);
	}
}
