const BITS: u32 = 64;

pub const NOT_FOUND: u32 = u32::MAX;

#[derive(Clone, Default)]
pub struct SlotSet
{
	words: Vec<u64>,
}

impl SlotSet
{
	pub fn new(max_bits: u32, all_set: bool) -> Self
	{
		let words = max_bits.div_ceil(BITS) as usize;

		Self {
			words: vec![if all_set { u64::MAX } else { 0 }; words],
		}
	}

	pub fn is_init(&self) -> bool
	{
		!self.words.is_empty()
	}

	pub fn capacity(&self) -> u64
	{
		self.words.len() as u64 * u64::from(BITS)
	}

	pub fn words(&self) -> &[u64]
	{
		&self.words
	}

	pub fn get(&self, index: u32) -> bool
	{
		self.words
			.get((index / BITS) as usize)
			.is_some_and(|word| word & (1 << (index % BITS)) != 0)
	}

	pub fn set(&mut self, index: u32)
	{
		if let Some(word) = self.words.get_mut((index / BITS) as usize) {
			*word |= 1 << (index % BITS);
		}
	}

	pub fn unset(&mut self, index: u32)
	{
		if let Some(word) = self.words.get_mut((index / BITS) as usize) {
			*word &= !(1 << (index % BITS));
		}
	}

	pub fn clear_all(&mut self)
	{
		self.words.fill(0);
	}

	pub fn find_next_free(&self, start: u32) -> u32
	{
		let skip = (!start.is_multiple_of(BITS)).then(|| (1u64 << (start % BITS)) - 1);

		for (index, word) in self.words.iter().enumerate().skip((start / BITS) as usize) {
			let mask = if index == (start / BITS) as usize {
				skip.unwrap_or(0)
			} else {
				0
			};

			let chunk = word | mask;

			if chunk != u64::MAX {
				return index as u32 * BITS + chunk.trailing_ones();
			}
		}

		NOT_FOUND
	}

	pub fn find_next_set(&self, start: u32) -> u32
	{
		for (index, word) in self.words.iter().enumerate().skip((start / BITS) as usize) {
			let mask = if index == (start / BITS) as usize && !start.is_multiple_of(BITS) {
				(1u64 << (start % BITS)) - 1
			} else {
				0
			};

			let chunk = word & !mask;

			if chunk != 0 {
				return index as u32 * BITS + chunk.trailing_zeros();
			}
		}

		NOT_FOUND
	}

	/// Makes room for an object at `current` to be followed by `instances` more. The object keeps
	/// its slot when the bits after it are free, otherwise it gives its slot up and a new run of
	/// `instances + 1` slots is taken. Returns where the run starts, and whether that is a move.
	pub fn reserve_instances(&mut self, current: u32, instances: u32) -> Option<(u32, bool)>
	{
		self.unset(current);

		let start = self.find_free_group(instances + 1);

		if start == NOT_FOUND {
			self.set(current);

			return None;
		}

		for bit in start..=start + instances {
			self.set(bit);
		}

		Some((start, start != current))
	}

	/// Finds the first run of `size` free bits that lies inside the set.
	pub fn find_free_group(&self, size: u32) -> u32
	{
		if size == 0 {
			return NOT_FOUND;
		}

		let capacity = self.capacity();

		let mut start = self.find_next_free(0);

		while u64::from(start) + u64::from(size) <= capacity && start != NOT_FOUND {
			match (start + 1..start + size).find(|bit| self.get(*bit)) {
				None => return start,
				Some(taken) => start = self.find_next_free(taken),
			}
		}

		NOT_FOUND
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn an_object_with_room_after_it_keeps_its_slot()
	{
		let mut slots = SlotSet::new(64, false);

		slots.set(0);

		assert_eq!(slots.reserve_instances(0, 3), Some((0, false)));
		assert!((0..=3).all(|bit| slots.get(bit)));
		assert!(!slots.get(4));
	}

	#[test]
	fn an_object_without_room_moves_to_a_free_run()
	{
		let mut slots = SlotSet::new(64, false);

		slots.set(0);
		slots.set(2);

		assert_eq!(slots.reserve_instances(0, 3), Some((3, true)));
		assert!(!slots.get(0));
		assert!((3..=6).all(|bit| slots.get(bit)));
	}

	#[test]
	fn a_full_set_leaves_the_object_where_it_was()
	{
		let mut slots = SlotSet::new(64, true);

		assert_eq!(slots.reserve_instances(5, 2), None);
		assert!(slots.get(5));
	}

	#[test]
	fn the_size_is_rounded_up_to_whole_words()
	{
		assert_eq!(SlotSet::new(1, false).capacity(), 64);
		assert_eq!(SlotSet::new(64, false).capacity(), 64);
		assert_eq!(SlotSet::new(65, false).capacity(), 128);
		assert!(!SlotSet::default().is_init());
	}

	#[test]
	fn bits_are_set_cleared_and_read_back()
	{
		let mut slots = SlotSet::new(200, false);

		slots.set(0);
		slots.set(63);
		slots.set(64);
		slots.set(199);

		assert!(slots.get(0) && slots.get(63) && slots.get(64) && slots.get(199));
		assert!(!slots.get(1) && !slots.get(65));

		slots.unset(63);

		assert!(!slots.get(63));

		slots.clear_all();

		assert!(!slots.get(0) && !slots.get(199));
	}

	#[test]
	fn out_of_range_bits_are_ignored()
	{
		let mut slots = SlotSet::new(64, false);

		slots.set(1000);

		assert!(!slots.get(1000));
	}

	#[test]
	fn the_next_free_bit_skips_set_ones_and_honours_the_start()
	{
		let mut slots = SlotSet::new(128, false);

		for bit in 0..70 {
			slots.set(bit);
		}

		assert_eq!(slots.find_next_free(0), 70);
		assert_eq!(slots.find_next_free(80), 80);
		assert_eq!(slots.find_next_free(127), 127);

		for bit in 70..128 {
			slots.set(bit);
		}

		assert_eq!(slots.find_next_free(0), NOT_FOUND);
		assert_eq!(slots.find_next_free(500), NOT_FOUND);
	}

	#[test]
	fn the_next_set_bit_honours_the_start()
	{
		let mut slots = SlotSet::new(128, false);

		slots.set(3);
		slots.set(100);

		assert_eq!(slots.find_next_set(0), 3);
		assert_eq!(slots.find_next_set(3), 3);
		assert_eq!(slots.find_next_set(4), 100);
		assert_eq!(slots.find_next_set(101), NOT_FOUND);
	}

	#[test]
	fn a_free_group_is_a_whole_run_of_free_bits()
	{
		let mut slots = SlotSet::new(64, false);

		slots.set(0);
		slots.set(3);

		assert_eq!(slots.find_free_group(2), 1);
		assert_eq!(slots.find_free_group(3), 4);

		for bit in 4..60 {
			slots.set(bit);
		}

		assert_eq!(slots.find_free_group(4), 60);
		assert_eq!(slots.find_free_group(5), NOT_FOUND);
		assert_eq!(slots.find_free_group(0), NOT_FOUND);
	}
}
