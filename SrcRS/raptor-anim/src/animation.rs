use crate::math::{Quat, lerp3, slerp};

#[derive(Clone, Debug, Default)]
pub struct Track<T>
{
	pub times: Vec<f32>,
	pub values: Vec<T>,
}

impl<T: Copy> Track<T>
{
	/// The value at `time`, held at the first or last key outside the keys, or `default` if the
	/// track has no keys.
	pub fn sample(&self, time: f32, default: T, blend: impl Fn(T, T, f32) -> T) -> T
	{
		let count = self.times.len().min(self.values.len());

		if count == 0 {
			return default;
		}

		if time <= self.times[0] {
			return self.values[0];
		}

		if time >= self.times[count - 1] {
			return self.values[count - 1];
		}

		let next = self.times[..count].partition_point(|key| *key <= time);

		if next == 0 || next >= count {
			return self.values[count - 1];
		}

		let (t0, t1) = (self.times[next - 1], self.times[next]);
		let duration = t1 - t0;
		let alpha = if duration > 1e-6 {
			(time - t0) / duration
		}
		else {
			0.0
		};

		blend(self.values[next - 1], self.values[next], alpha)
	}
}

#[derive(Clone, Debug, Default)]
pub struct BoneTrack
{
	pub translation: Track<[f32; 3]>,
	pub rotation: Track<Quat>,
	pub scale: Track<[f32; 3]>,
}

impl BoneTrack
{
	pub fn translation_at(&self, time: f32, default: [f32; 3]) -> [f32; 3]
	{
		self.translation.sample(time, default, lerp3)
	}

	pub fn rotation_at(&self, time: f32, default: Quat) -> Quat
	{
		self.rotation.sample(time, default, slerp)
	}

	pub fn scale_at(&self, time: f32, default: [f32; 3]) -> [f32; 3]
	{
		self.scale.sample(time, default, lerp3)
	}
}

#[derive(Clone, Debug, Default)]
pub struct Animation
{
	pub name: String,
	pub duration: f32,
	pub tracks: Vec<BoneTrack>,
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn track() -> Track<[f32; 3]>
	{
		Track {
			times: vec![0.0, 1.0, 3.0],
			values: vec![[0.0, 0.0, 0.0], [10.0, 0.0, 0.0], [30.0, 0.0, 0.0]],
		}
	}

	#[test]
	fn an_empty_track_gives_the_default()
	{
		let empty = Track::<[f32; 3]>::default();

		assert_eq!(empty.sample(5.0, [1.0, 2.0, 3.0], lerp3), [1.0, 2.0, 3.0]);
	}

	#[test]
	fn times_outside_the_keys_hold_the_end_values()
	{
		assert_eq!(track().sample(-1.0, [9.0; 3], lerp3), [0.0, 0.0, 0.0]);
		assert_eq!(track().sample(7.0, [9.0; 3], lerp3), [30.0, 0.0, 0.0]);
	}

	#[test]
	fn times_between_keys_blend_the_two_around_them()
	{
		assert_eq!(track().sample(0.5, [9.0; 3], lerp3), [5.0, 0.0, 0.0]);
		assert_eq!(track().sample(2.0, [9.0; 3], lerp3), [20.0, 0.0, 0.0]);
	}

	#[test]
	fn a_key_exactly_on_the_time_gives_that_key()
	{
		assert_eq!(track().sample(1.0, [9.0; 3], lerp3), [10.0, 0.0, 0.0]);
	}

	#[test]
	fn a_time_that_is_not_a_number_gives_the_last_key()
	{
		assert_eq!(track().sample(f32::NAN, [9.0; 3], lerp3), [30.0, 0.0, 0.0]);
	}

	#[test]
	fn keys_at_the_same_time_do_not_divide_by_zero()
	{
		let stepped = Track {
			times: vec![0.0, 1.0, 1.0, 2.0],
			values: vec![[0.0; 3], [1.0; 3], [5.0; 3], [6.0; 3]],
		};

		assert_eq!(stepped.sample(1.0, [9.0; 3], lerp3), [5.0; 3]);
		assert!(stepped.sample(1.5, [9.0; 3], lerp3)[0].is_finite());
	}
}
