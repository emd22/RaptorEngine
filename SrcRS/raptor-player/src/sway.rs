const AMOUNT: f32 = 0.12;
const RETURN_SPEED: f32 = 12.0;
const MAX_SWAY: f32 = 5.0 * (std::f64::consts::PI / 180.0) as f32;

unsafe extern "C" {
	fn remainderf(x: f32, y: f32) -> f32;
}

fn smooth_interpolate_to_zero(a: f32, speed: f32, delta_time: f32) -> f32
{
	let t = 1.0 - (-speed * delta_time).exp();

	t * 0.0 + (1.0 - t) * a
}

/// How far the view model trails the camera, in radians.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ViewSway
{
	pub yaw: f32,
	pub pitch: f32,
	pub prev_yaw: f32,
	pub prev_pitch: f32,
}

impl ViewSway
{
	pub fn update(&mut self, camera_yaw: f32, camera_pitch: f32, delta_time: f32)
	{
		// SAFETY: `remainderf` is the C library's pure function on two floats.
		let yaw_delta = unsafe { remainderf(camera_yaw - self.prev_yaw, std::f64::consts::TAU as f32) };
		let pitch_delta = camera_pitch - self.prev_pitch;

		self.prev_yaw = camera_yaw;
		self.prev_pitch = camera_pitch;

		self.yaw = smooth_interpolate_to_zero(self.yaw, RETURN_SPEED, delta_time);
		self.pitch = smooth_interpolate_to_zero(self.pitch, RETURN_SPEED, delta_time);

		self.yaw = (-yaw_delta).mul_add(AMOUNT, self.yaw).clamp(-MAX_SWAY, MAX_SWAY);
		self.pitch = (-pitch_delta).mul_add(AMOUNT, self.pitch).clamp(-MAX_SWAY, MAX_SWAY);
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn the_view_model_trails_a_turn_and_settles()
	{
		let mut sway = ViewSway::default();

		sway.update(0.1, 0.0, 1.0 / 60.0);
		assert!(sway.yaw < 0.0);

		for _ in 0..120 {
			sway.update(0.1, 0.0, 1.0 / 60.0);
		}

		assert!(sway.yaw.abs() < 1e-6);
	}

	#[test]
	fn a_yaw_wrap_is_not_a_full_turn()
	{
		let mut sway = ViewSway {
			prev_yaw: 6.27,
			..ViewSway::default()
		};

		sway.update(0.01, 0.0, 1.0 / 60.0);

		assert!(sway.yaw.abs() < 0.01);
	}

	#[test]
	fn the_sway_is_limited()
	{
		let mut sway = ViewSway::default();

		sway.update(0.0, 3.0, 1.0 / 60.0);

		assert_eq!(sway.pitch, -MAX_SWAY);
	}
}
