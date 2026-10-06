use crate::blood::COOLDOWN;

pub const MAX_DUMMIES: usize = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct DummyHandle(pub u32);

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Impact {
	pub serial: u64,
	pub point: [f32; 3],
	pub normal: [f32; 3],
	pub speed: f32,
}

#[derive(Debug)]
pub struct Dummy {
	pub handle: DummyHandle,
	pub serial: u64,
	pub blood_cooldown: f32,
}

#[derive(Default)]
pub struct DummySet {
	dummies: Vec<Dummy>,
	spawn_count: u32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Placement {
	pub position: [f32; 3],
	pub forward: [f32; 3],
}

impl DummySet {
	pub fn is_empty(&self) -> bool {
		self.dummies.is_empty()
	}

	pub fn len(&self) -> usize {
		self.dummies.len()
	}

	pub fn iter(&self) -> impl Iterator<Item = &Dummy> {
		self.dummies.iter()
	}

	pub fn spawn_count(&self) -> u32 {
		self.spawn_count
	}

	pub fn placement(
		&mut self,
		player: [f32; 3],
		camera_forward: [f32; 3],
		camera_right: [f32; 3],
	) -> Placement {
		let mut forward = [camera_forward[0], 0.0, camera_forward[2]];
		let length = (forward[0] * forward[0] + forward[2] * forward[2]).sqrt();

		if length > f32::EPSILON {
			forward = [forward[0] / length, 0.0, forward[2] / length];
		}

		let lateral = ((self.spawn_count % 3) as f32 - 1.0) * 0.9;
		let height = 1.0 + 0.3 * (self.spawn_count % 2) as f32;

		self.spawn_count += 1;

		Placement {
			position: [
				player[0] + forward[0] * 1.5 + camera_right[0] * lateral,
				player[1] + forward[1] * 1.5 + camera_right[1] * lateral + height,
				player[2] + forward[2] * 1.5 + camera_right[2] * lateral,
			],
			forward,
		}
	}

	pub fn needs_eviction(&self) -> bool {
		self.dummies.len() >= MAX_DUMMIES
	}

	pub fn evict_oldest(&mut self) -> Option<Dummy> {
		(!self.dummies.is_empty()).then(|| self.dummies.remove(0))
	}

	pub fn add(&mut self, handle: DummyHandle, serial: u64) {
		self.dummies.push(Dummy {
			handle,
			serial,
			blood_cooldown: 0.0,
		});
	}

	pub fn drain(&mut self) -> Vec<Dummy> {
		std::mem::take(&mut self.dummies)
	}

	pub fn tick_cooldowns(&mut self, delta_time: f32) {
		for dummy in &mut self.dummies {
			dummy.blood_cooldown = (dummy.blood_cooldown - delta_time).max(0.0);
		}
	}

	pub fn take_blood_slot(&mut self, serial: u64) -> bool {
		match self.dummies.iter_mut().find(|dummy| dummy.serial == serial) {
			Some(dummy) if dummy.blood_cooldown <= 0.0 => {
				dummy.blood_cooldown = COOLDOWN;
				true
			}
			_ => false,
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn placements_fan_out_in_front_of_the_player() {
		let mut set = DummySet::default();

		let first = set.placement([0.0; 3], [0.0, 0.3, 1.0], [1.0, 0.0, 0.0]);
		let second = set.placement([0.0; 3], [0.0, 0.3, 1.0], [1.0, 0.0, 0.0]);

		assert_eq!(first.forward, [0.0, 0.0, 1.0]);
		assert!((first.position[0] + 0.9).abs() < 1e-5);
		assert!((first.position[1] - 1.0).abs() < 1e-5);
		assert!((second.position[1] - 1.3).abs() < 1e-5);
		assert_eq!(set.spawn_count(), 2);
	}

	#[test]
	fn the_oldest_dummy_goes_first_once_full() {
		let mut set = DummySet::default();

		for index in 0..MAX_DUMMIES as u32 {
			assert!(!set.needs_eviction());
			set.add(DummyHandle(index), u64::from(index));
		}

		assert!(set.needs_eviction());
		assert_eq!(
			set.evict_oldest().map(|dummy| dummy.handle),
			Some(DummyHandle(0))
		);
		assert!(!set.needs_eviction());
	}

	#[test]
	fn blood_respects_the_cooldown_per_dummy() {
		let mut set = DummySet::default();

		set.add(DummyHandle(0), 7);

		assert!(set.take_blood_slot(7));
		assert!(!set.take_blood_slot(7));
		assert!(!set.take_blood_slot(9));

		set.tick_cooldowns(0.3);
		assert!(set.take_blood_slot(7));
	}
}
