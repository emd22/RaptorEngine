use std::collections::HashMap;
use std::mem::size_of;
use std::sync::Mutex;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

pub const INVALID_INDEX: u32 = u32::MAX;
pub const NUM_PASSES: usize = 7;
pub const NUM_FEATURE_COMBINATIONS: usize = 1 << 4;

const FNV64_INIT: u64 = 0xCBF2_9CE4_8422_2325;
const FNV64_PRIME: u64 = 0x0000_0100_0000_01B3;

pub fn fnv64(mut hash: u64, bytes: &[u8]) -> u64
{
	for byte in bytes {
		hash ^= u64::from(*byte);
		hash = hash.wrapping_mul(FNV64_PRIME);
	}

	hash
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PipelineKey
{
	pub macro_hash: u64,
	pub blend_hash: u64,
	pub pass_hash: u64,
	pub layout_hash: u64,
	pub shader: u32,
	pub vertex_type: u32,
	pub cull_mode: u32,
	pub winding_order: i32,
	pub polygon_mode: i32,
	pub depth_compare_op: i32,
	pub is_compute: u8,
	pub has_vertex_input: u8,
	pub depth_test: u8,
	pub depth_write: u8,
	pub render_lines: u8,
	pub reserved: [u8; 3],
}

const _: () = {
	assert!(size_of::<PipelineKey>() == 64);
	assert!(4 * 8 + 4 * 2 + 4 * 4 + 5 + 3 == size_of::<PipelineKey>());
};

impl PipelineKey
{
	pub fn hash(&self) -> u64
	{
		// SAFETY: the struct is `repr(C)` with no padding (the const assert above sums the field
		// sizes to its size) and only holds integers, so every byte is initialised.
		let bytes = unsafe {
			std::slice::from_raw_parts(std::ptr::from_ref(self).cast::<u8>(), size_of::<Self>())
		};

		fnv64(FNV64_INIT, bytes)
	}
}

pub fn mix_hash(hash: u64, bytes: &[u8]) -> u64
{
	fnv64(hash, bytes)
}

pub fn hash_init() -> u64
{
	FNV64_INIT
}

pub fn blend_hash(states: &[[u32; 8]]) -> u64
{
	let mut hash = FNV64_INIT;

	for state in states {
		for field in state {
			hash = fnv64(hash, &field.to_ne_bytes());
		}
	}

	hash
}

pub fn pass_hash(attachments: &[(u32, u32)]) -> u64
{
	let mut hash = FNV64_INIT;

	for (format, samples) in attachments {
		hash = fnv64(hash, &format.to_ne_bytes());
		hash = fnv64(hash, &samples.to_ne_bytes());
	}

	hash
}

pub fn layout_hash(sets: &[(u32, u32)], push_constants: &[(u32, u32)]) -> u64
{
	let mut hash = FNV64_INIT;

	for (set_index, layout_id) in sets {
		hash = fnv64(hash, &set_index.to_ne_bytes());
		hash = fnv64(hash, &layout_id.to_ne_bytes());
	}

	for (size, stages) in push_constants {
		hash = fnv64(hash, &size.to_ne_bytes());
		hash = fnv64(hash, &stages.to_ne_bytes());
	}

	hash
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KeyRegistration
{
	Registered,
	IdenticalKey
	{
		other: u32,
		hash: u64,
	},
	HashCollision
	{
		other: u32,
		hash: u64,
	},
}

#[derive(Clone, Copy, Default)]
struct KeyEntry
{
	key: PipelineKey,
	hash: u64,
	registered: bool,
}

#[derive(Clone, Copy)]
struct VariantInfo
{
	pass: Option<u32>,
	features: u32,
}

#[derive(Default)]
struct Inner
{
	keys: Vec<KeyEntry>,
	variant_infos: Vec<VariantInfo>,
	key_lookup: HashMap<u64, u32>,
	pass_pipelines: [Vec<u32>; NUM_PASSES],
	pending: Vec<u32>,
	dynamic_count: u32,
}

pub struct PipelineRegistry
{
	num_static: u32,
	max_dynamic: u32,
	variants: [[AtomicU32; NUM_FEATURE_COMBINATIONS]; NUM_PASSES],
	has_pending: AtomicBool,
	inner: Mutex<Inner>,
}

impl PipelineRegistry
{
	pub fn new(num_static: u32, max_dynamic: u32) -> Self
	{
		let inner = Inner {
			keys: vec![KeyEntry::default(); num_static as usize],
			variant_infos: vec![
				VariantInfo {
					pass: None,
					features: 0
				};
				num_static as usize
			],
			..Default::default()
		};

		Self {
			num_static,
			max_dynamic,
			variants: std::array::from_fn(|_| {
				std::array::from_fn(|_| AtomicU32::new(INVALID_INDEX))
			}),
			has_pending: AtomicBool::new(false),
			inner: Mutex::new(inner),
		}
	}

	fn lock(&self) -> std::sync::MutexGuard<'_, Inner>
	{
		self.inner
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn allocate(&self) -> Option<u32>
	{
		let mut inner = self.lock();

		if inner.dynamic_count >= self.max_dynamic {
			return None;
		}

		let index = inner.dynamic_count;
		inner.dynamic_count += 1;

		inner.keys.push(KeyEntry::default());
		inner.variant_infos.push(VariantInfo {
			pass: None,
			features: 0,
		});

		inner.pending.push(index);
		self.has_pending.store(true, Ordering::Release);

		Some(self.num_static + index)
	}

	pub fn has_pending(&self) -> bool
	{
		self.has_pending.load(Ordering::Acquire)
	}

	pub fn take_pending(&self) -> Vec<u32>
	{
		let mut inner = self.lock();
		self.has_pending.store(false, Ordering::Release);

		std::mem::take(&mut inner.pending)
	}

	pub fn dynamic_count(&self) -> u32
	{
		self.lock().dynamic_count
	}

	pub fn register_key(&self, handle: u32, key: &PipelineKey) -> KeyRegistration
	{
		let mut inner = self.lock();

		assert!((handle as usize) < inner.keys.len());

		let entry = inner.keys[handle as usize];

		if entry.registered && inner.key_lookup.get(&entry.hash) == Some(&handle) {
			inner.key_lookup.remove(&entry.hash);
		}

		let hash = key.hash();

		inner.keys[handle as usize] = KeyEntry {
			key: *key,
			hash,
			registered: true,
		};

		match inner.key_lookup.get(&hash).copied() {
			None => {
				inner.key_lookup.insert(hash, handle);
				KeyRegistration::Registered
			}
			Some(existing) if existing == handle => KeyRegistration::Registered,
			Some(existing) => {
				if inner.keys[existing as usize].key == *key {
					KeyRegistration::IdenticalKey {
						other: existing,
						hash,
					}
				} else {
					KeyRegistration::HashCollision {
						other: existing,
						hash,
					}
				}
			}
		}
	}

	pub fn find(&self, key: &PipelineKey) -> Option<u32>
	{
		let inner = self.lock();

		let handle = *inner.key_lookup.get(&key.hash())?;

		(inner.keys[handle as usize].key == *key).then_some(handle)
	}

	pub fn key(&self, handle: u32) -> Option<PipelineKey>
	{
		let inner = self.lock();

		inner
			.keys
			.get(handle as usize)
			.filter(|entry| entry.registered)
			.map(|entry| entry.key)
	}

	pub fn register_variant(&self, pass: u32, features: u32, handle: u32)
	{
		let mut inner = self.lock();

		assert!((pass as usize) < NUM_PASSES);
		assert!((features as usize) < NUM_FEATURE_COMBINATIONS);
		assert!((handle as usize) < inner.variant_infos.len());

		self.variants[pass as usize][features as usize].store(handle, Ordering::Release);

		let pipelines = &mut inner.pass_pipelines[pass as usize];

		if !pipelines.contains(&handle) {
			pipelines.push(handle);
		}

		let info = &mut inner.variant_infos[handle as usize];

		if info.pass.is_none() {
			*info = VariantInfo {
				pass: Some(pass),
				features,
			};
		}
	}

	pub fn find_variant(&self, pass: u32, features: u32) -> u32
	{
		if pass as usize >= NUM_PASSES || features as usize >= NUM_FEATURE_COMBINATIONS {
			return INVALID_INDEX;
		}

		self.variants[pass as usize][features as usize].load(Ordering::Acquire)
	}

	pub fn variant_info(&self, handle: u32) -> Option<(u32, u32)>
	{
		let inner = self.lock();

		let info = inner.variant_infos.get(handle as usize)?;

		info.pass.map(|pass| (pass, info.features))
	}

	pub fn pass_pipeline(&self, pass: u32, index: usize) -> Option<u32>
	{
		self.lock()
			.pass_pipelines
			.get(pass as usize)?
			.get(index)
			.copied()
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn key(shader: u32) -> PipelineKey
	{
		PipelineKey {
			shader,
			macro_hash: 7,
			depth_test: 1,
			..Default::default()
		}
	}

	#[test]
	fn key_hash_covers_every_field_and_is_stable()
	{
		let base = key(1);

		assert_eq!(base.hash(), key(1).hash());
		assert_ne!(base.hash(), key(2).hash());
		assert_ne!(
			base.hash(),
			PipelineKey {
				blend_hash: 1,
				..base
			}
			.hash()
		);
		assert_ne!(
			base.hash(),
			PipelineKey {
				render_lines: 1,
				..base
			}
			.hash()
		);
		assert_ne!(
			base.hash(),
			PipelineKey {
				depth_compare_op: 3,
				..base
			}
			.hash()
		);
	}

	#[test]
	fn the_zero_key_hash_is_the_fnv_of_64_zero_bytes()
	{
		assert_eq!(PipelineKey::default().hash(), fnv64(FNV64_INIT, &[0u8; 64]));
	}

	#[test]
	fn sub_hashes_fold_in_order()
	{
		assert_eq!(blend_hash(&[]), FNV64_INIT);
		assert_ne!(pass_hash(&[(37, 1)]), pass_hash(&[(38, 1)]));
		assert_ne!(
			pass_hash(&[(37, 1), (50, 1)]),
			pass_hash(&[(50, 1), (37, 1)])
		);
		assert_eq!(
			layout_hash(&[(0, 5)], &[(64, 3)]),
			fnv64(
				fnv64(
					fnv64(fnv64(FNV64_INIT, &0u32.to_ne_bytes()), &5u32.to_ne_bytes()),
					&64u32.to_ne_bytes()
				),
				&3u32.to_ne_bytes()
			)
		);
		assert_ne!(layout_hash(&[(0, 5)], &[]), layout_hash(&[(0, 6)], &[]));
		assert_ne!(layout_hash(&[], &[(64, 3)]), layout_hash(&[], &[(64, 1)]));
	}

	#[test]
	fn handles_come_after_the_static_pipelines_and_run_out()
	{
		let registry = PipelineRegistry::new(9, 2);

		assert_eq!(registry.allocate(), Some(9));
		assert_eq!(registry.allocate(), Some(10));
		assert_eq!(registry.allocate(), None);
		assert_eq!(registry.dynamic_count(), 2);
	}

	#[test]
	fn pending_pipelines_are_taken_once()
	{
		let registry = PipelineRegistry::new(3, 8);

		assert!(!registry.has_pending());
		registry.allocate();
		registry.allocate();
		assert!(registry.has_pending());

		assert_eq!(registry.take_pending(), vec![0, 1]);
		assert!(!registry.has_pending());
		assert!(registry.take_pending().is_empty());
	}

	#[test]
	fn keys_register_find_and_report_duplicates()
	{
		let registry = PipelineRegistry::new(2, 8);
		let a = key(1);

		assert_eq!(registry.register_key(0, &a), KeyRegistration::Registered);
		assert_eq!(registry.find(&a), Some(0));
		assert_eq!(registry.find(&key(2)), None);
		assert_eq!(registry.key(0), Some(a));
		assert_eq!(registry.key(1), None);

		assert_eq!(registry.register_key(0, &a), KeyRegistration::Registered);

		assert_eq!(
			registry.register_key(1, &a),
			KeyRegistration::IdenticalKey {
				other: 0,
				hash: a.hash()
			}
		);
		assert_eq!(registry.find(&a), Some(0));
	}

	#[test]
	fn rebuilding_a_pipeline_replaces_its_old_key()
	{
		let registry = PipelineRegistry::new(1, 8);

		registry.register_key(0, &key(1));
		registry.register_key(0, &key(2));

		assert_eq!(registry.find(&key(1)), None);
		assert_eq!(registry.find(&key(2)), Some(0));
	}

	#[test]
	fn variants_are_found_by_pass_and_features_and_keep_their_first_registration()
	{
		let registry = PipelineRegistry::new(1, 8);
		let handle = registry.allocate().unwrap();

		assert_eq!(registry.find_variant(1, 3), INVALID_INDEX);
		assert_eq!(registry.find_variant(99, 3), INVALID_INDEX);

		registry.register_variant(1, 3, handle);
		registry.register_variant(2, 4, handle);

		assert_eq!(registry.find_variant(1, 3), handle);
		assert_eq!(registry.find_variant(2, 4), handle);
		assert_eq!(registry.variant_info(handle), Some((1, 3)));
		assert_eq!(registry.variant_info(0), None);

		assert_eq!(registry.pass_pipeline(1, 0), Some(handle));
		assert_eq!(registry.pass_pipeline(1, 1), None);

		registry.register_variant(1, 5, handle);
		assert_eq!(registry.pass_pipeline(1, 1), None);
	}
}
