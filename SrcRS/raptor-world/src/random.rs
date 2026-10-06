use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};

const SEED_32: u32 = 2_463_534_242;
const SEED_64: u64 = 88_172_645_463_325_252;

static STATE_32: AtomicU32 = AtomicU32::new(SEED_32);
static STATE_64: AtomicU64 = AtomicU64::new(SEED_64);

const UNIT_BITS: u32 = 24;

fn next_32(state: u32) -> u32
{
	let state = state ^ (state << 13);
	let state = state ^ (state >> 17);

	state ^ (state << 5)
}

fn next_64(state: u64) -> u64
{
	let state = state ^ (state << 13);
	let state = state ^ (state >> 7);

	state ^ (state << 17)
}

/// A 32-bit xorshift value.
pub fn fast_rand32() -> u32
{
	let next = next_32(STATE_32.load(Ordering::Relaxed));

	STATE_32.store(next, Ordering::Relaxed);

	next
}

/// A 64-bit xorshift value.
pub fn fast_rand64() -> u64
{
	let next = next_64(STATE_64.load(Ordering::Relaxed));

	STATE_64.store(next, Ordering::Relaxed);

	next
}

/// A value in `[0, 1)` with 24 bits of precision.
pub fn unit() -> f32
{
	(fast_rand32() >> (32 - UNIT_BITS)) as f32 / (1u32 << UNIT_BITS) as f32
}

/// A value in `[-1, 1]` with 24 bits of precision.
pub fn signed_unit() -> f32
{
	(fast_rand32() >> (32 - UNIT_BITS)) as f32 * (2.0 / ((1u32 << UNIT_BITS) - 1) as f32) - 1.0
}

pub fn range(low: f32, high: f32) -> f32
{
	low + (high - low) * unit()
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn the_sequence_matches_xorshift32_from_its_seed()
	{
		let first = next_32(SEED_32);
		let second = next_32(first);

		assert_ne!(first, SEED_32);
		assert_ne!(first, second);
		assert_eq!(first, 723_471_715);
	}

	#[test]
	fn units_stay_in_range()
	{
		for _ in 0..10_000 {
			let value = unit();
			let signed = signed_unit();

			assert!((0.0..1.0).contains(&value));
			assert!((-1.0..=1.0).contains(&signed));
		}
	}

	#[test]
	fn a_range_stays_between_its_ends()
	{
		for _ in 0..1000 {
			let value = range(-5.0, 3.0);

			assert!((-5.0..3.0).contains(&value));
		}
	}
}
