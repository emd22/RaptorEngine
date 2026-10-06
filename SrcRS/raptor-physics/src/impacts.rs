use std::sync::Mutex;
use std::sync::atomic::{AtomicU32, Ordering};

pub const RAGDOLL_TAG: u64 = 0x5247_4C44_0000_0000;
pub const RAGDOLL_TAG_MASK: u64 = 0xFFFF_FFFF_0000_0000;
pub const MAX_QUEUED_IMPACTS: usize = 64;

pub fn ragdoll_user_data(serial: u32) -> u64
{
	RAGDOLL_TAG | u64::from(serial)
}

pub fn is_ragdoll(user_data: u64) -> bool
{
	user_data & RAGDOLL_TAG_MASK == RAGDOLL_TAG
}

pub fn ragdoll_serial(user_data: u64) -> u32
{
	(user_data & 0xFFFF_FFFF) as u32
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RagdollSide
{
	/// Neither body is part of a ragdoll, or both are
	Neither,
	First,
	Second,
}

/// Which body of a contact is part of a ragdoll, if exactly one is.
pub fn ragdoll_side(first_user_data: u64, second_user_data: u64) -> RagdollSide
{
	match (is_ragdoll(first_user_data), is_ragdoll(second_user_data)) {
		(true, false) => RagdollSide::First,
		(false, true) => RagdollSide::Second,
		_ => RagdollSide::Neither,
	}
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Impact
{
	pub point: [f32; 3],
	pub normal: [f32; 3],
	pub speed: f32,
	pub ragdoll_serial: u32,
}

/// The hard hits of ragdolls on surfaces since they were last collected. Contacts are reported
/// from the physics worker threads, so this can be used from any of them.
pub struct ImpactQueue
{
	min_speed: AtomicU32,
	impacts: Mutex<Vec<Impact>>,
}

impl Default for ImpactQueue
{
	fn default() -> Self
	{
		Self {
			min_speed: AtomicU32::new(1.0e9f32.to_bits()),
			impacts: Mutex::new(Vec::new()),
		}
	}
}

impl ImpactQueue
{
	pub fn set_min_speed(&self, speed: f32)
	{
		self.min_speed.store(speed.to_bits(), Ordering::Relaxed);
	}

	/// Queues an impact if it is hard enough and there is room. Returns whether it was queued.
	pub fn offer(&self, impact: Impact) -> bool
	{
		if impact.speed < f32::from_bits(self.min_speed.load(Ordering::Relaxed)) {
			return false;
		}

		let mut impacts = self
			.impacts
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		if impacts.len() >= MAX_QUEUED_IMPACTS {
			return false;
		}

		impacts.push(impact);

		true
	}

	pub fn drain(&self) -> Vec<Impact>
	{
		std::mem::take(
			&mut *self
				.impacts
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner()),
		)
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn impact(speed: f32) -> Impact
	{
		Impact {
			speed,
			ragdoll_serial: 3,
			..Default::default()
		}
	}

	#[test]
	fn the_user_data_of_a_ragdoll_body_carries_its_serial()
	{
		let data = ragdoll_user_data(42);

		assert!(is_ragdoll(data));
		assert_eq!(ragdoll_serial(data), 42);
		assert!(!is_ragdoll(42));
		assert!(!is_ragdoll(0));
	}

	#[test]
	fn only_a_contact_between_a_ragdoll_and_something_else_has_a_ragdoll_side()
	{
		let ragdoll = ragdoll_user_data(1);

		assert_eq!(ragdoll_side(ragdoll, 0), RagdollSide::First);
		assert_eq!(ragdoll_side(0, ragdoll), RagdollSide::Second);
		assert_eq!(ragdoll_side(ragdoll, ragdoll), RagdollSide::Neither);
		assert_eq!(ragdoll_side(0, 0), RagdollSide::Neither);
	}

	#[test]
	fn impacts_below_the_minimum_speed_are_dropped_and_none_are_kept_by_default()
	{
		let queue = ImpactQueue::default();

		assert!(!queue.offer(impact(50.0)));

		queue.set_min_speed(5.0);

		assert!(!queue.offer(impact(4.9)));
		assert!(queue.offer(impact(5.0)));
		assert_eq!(queue.drain().len(), 1);
		assert!(queue.drain().is_empty());
	}

	#[test]
	fn the_queue_holds_a_bounded_number_of_impacts()
	{
		let queue = ImpactQueue::default();

		queue.set_min_speed(0.0);

		for _ in 0..MAX_QUEUED_IMPACTS {
			assert!(queue.offer(impact(1.0)));
		}

		assert!(!queue.offer(impact(1.0)));
		assert_eq!(queue.drain().len(), MAX_QUEUED_IMPACTS);
		assert!(queue.offer(impact(1.0)));
	}
}
