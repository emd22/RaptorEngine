/// Channels of the kick, in the order the engine stores them
pub const PITCH: usize = 0;
pub const YAW: usize = 1;
pub const ROLL: usize = 2;
pub const BACK: usize = 3;
pub const CHANNELS: usize = 4;

const FREQUENCY: f32 = 22.0;
const DAMPING: f32 = 0.55;
const LIMIT: f32 = 2.5;
const YAW_RATIO: f32 = 0.3;
const ROLL_RATIO: f32 = 0.5;

/// A damped spring on each channel. A shot adds velocity, and the value settles back to zero.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ViewKick
{
	pub value: [f32; CHANNELS],
	pub velocity: [f32; CHANNELS],
	pub bound: [f32; CHANNELS],
}

/// The velocity that makes a spring peak at exactly 1.
pub fn impulse_per_peak() -> f32
{
	let omega = FREQUENCY;
	let zeta = DAMPING;
	let damped = omega * (1.0 - zeta * zeta).sqrt();
	let peak_time = damped.atan2(zeta * omega) / damped;
	let peak_per_impulse = (-zeta * omega * peak_time).exp() * (damped * peak_time).sin() / damped;

	1.0 / peak_per_impulse
}

impl ViewKick
{
	/// `random_yaw` and `random_roll` are in [-1, 1].
	pub fn fire(&mut self, kick_degrees: f32, kickback: f32, random_yaw: f32, random_roll: f32)
	{
		let pitch = kick_degrees * (std::f64::consts::PI / 180.0) as f32;

		let peaks = [
			pitch,
			pitch * YAW_RATIO * random_yaw,
			pitch * ROLL_RATIO * random_roll,
			kickback,
		];

		let bounds = [pitch, pitch * YAW_RATIO, pitch * ROLL_RATIO, kickback];

		let impulse = impulse_per_peak();

		for i in 0..CHANNELS {
			self.velocity[i] += peaks[i] * impulse;
			self.bound[i] = self.bound[i].max(bounds[i] * LIMIT);
		}
	}

	pub fn update(&mut self, delta_time: f32)
	{
		let omega = FREQUENCY;
		let zeta = DAMPING;
		let damped = omega * (1.0 - zeta * zeta).sqrt();

		let decay = (-zeta * omega * delta_time).exp();
		let cos_step = (damped * delta_time).cos();
		let sin_step = (damped * delta_time).sin();

		for i in 0..CHANNELS {
			let x = self.value[i];
			let v = self.velocity[i];

			let next_x = decay * (x * cos_step + ((v + zeta * omega * x) / damped) * sin_step);
			let next_v = decay * (v * cos_step - ((omega * omega * x + zeta * omega * v) / damped) * sin_step);

			self.value[i] = next_x.clamp(-self.bound[i], self.bound[i]);
			self.velocity[i] = next_v;
		}
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn a_shot_peaks_at_the_requested_kick_and_settles()
	{
		let mut kick = ViewKick::default();

		kick.fire(2.5, 0.025, 0.0, 0.0);

		let mut peak = 0.0f32;

		for _ in 0..600 {
			kick.update(1.0 / 600.0);
			peak = peak.max(kick.value[PITCH]);
		}

		let wanted = 2.5 * (std::f64::consts::PI / 180.0) as f32;

		assert!((peak - wanted).abs() < wanted * 0.01, "{peak} vs {wanted}");

		for _ in 0..600 {
			kick.update(1.0 / 60.0);
		}

		assert!(kick.value[PITCH].abs() < 1e-6);
	}

	#[test]
	fn the_value_never_leaves_its_bound()
	{
		let mut kick = ViewKick::default();

		for _ in 0..8 {
			kick.fire(2.5, 0.025, 1.0, -1.0);
		}

		for _ in 0..240 {
			kick.update(1.0 / 120.0);

			for i in 0..CHANNELS {
				assert!(kick.value[i].abs() <= kick.bound[i]);
			}
		}
	}
}
