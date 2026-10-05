use std::ffi::c_void;

use ash::vk;
use raptor_gpu::{
	DescriptorSlot, ReflectionEntry, blend_hash, filter_by_input_mask, find_missing_descriptor, layout_hash, pass_hash,
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxHashPair
{
	pub first: u32,
	pub second: u32,
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_pipeline_hash_init() -> u64
{
	raptor_gpu::hash_init()
}

/// # Safety
///
/// `states` must be valid for `count` `VkPipelineColorBlendAttachmentState`s (eight 32 bit fields
/// each).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_blend_hash(states: *const c_void, count: usize) -> u64
{
	if count == 0 {
		return blend_hash(&[]);
	}

	// SAFETY: guaranteed by the caller.
	blend_hash(unsafe { std::slice::from_raw_parts(states.cast::<[u32; 8]>(), count) })
}

/// # Safety
///
/// `pairs` must be valid for `count` entries.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_pass_hash(pairs: *const RxHashPair, count: usize) -> u64
{
	let pairs: Vec<_> = if count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(pairs, count) }
			.iter()
			.map(|pair| (pair.first, pair.second))
			.collect()
	};

	pass_hash(&pairs)
}

/// # Safety
///
/// `sets` and `push_constants` must be valid for their counts.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_layout_hash(
	sets: *const RxHashPair,
	set_count: usize,
	push_constants: *const RxHashPair,
	push_count: usize,
) -> u64
{
	let collect = |pairs: *const RxHashPair, count: usize| -> Vec<(u32, u32)> {
		if count == 0 {
			return Vec::new();
		}

		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(pairs, count) }
			.iter()
			.map(|pair| (pair.first, pair.second))
			.collect()
	};

	layout_hash(
		&collect(sets, set_count),
		&collect(push_constants, push_count),
	)
}

/// # Safety
///
/// `attributes` must be valid and writable for `count` `VkVertexInputAttributeDescription`s.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_vertex_filter_attributes(
	attributes: *mut c_void,
	count: usize,
	mask: u32,
) -> usize
{
	if count == 0 {
		return 0;
	}

	let attributes = attributes.cast::<vk::VertexInputAttributeDescription>();

	// SAFETY: guaranteed by the caller.
	let kept = filter_by_input_mask(
		unsafe { std::slice::from_raw_parts(attributes, count) },
		mask,
	);

	// SAFETY: guaranteed by the caller; `kept` is no longer than the input.
	unsafe { std::ptr::copy_nonoverlapping(kept.as_ptr(), attributes, kept.len()) };

	kept.len()
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxDescriptorSlot
{
	pub set: u32,
	pub binding: u32,
	pub kind: u32,
}

/// # Safety
///
/// `reflection` and `slots` must be valid for their counts, and `out_missing` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_check_descriptors(
	reflection: *const crate::RxReflectionEntry,
	reflection_count: usize,
	slots: *const RxDescriptorSlot,
	slot_count: usize,
	out_missing: *mut RxDescriptorSlot,
) -> i32
{
	let reflection: Vec<_> = if reflection_count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(reflection, reflection_count) }
			.iter()
			.map(|entry| ReflectionEntry {
				kind: entry.kind,
				set: entry.set,
				binding: entry.binding,
			})
			.collect()
	};

	let slots: Vec<_> = if slot_count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(slots, slot_count) }
			.iter()
			.map(|slot| DescriptorSlot {
				set: slot.set,
				binding: slot.binding,
				kind: slot.kind,
			})
			.collect()
	};

	match find_missing_descriptor(&reflection, &slots) {
		Some(missing) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*out_missing = RxDescriptorSlot {
					set: missing.set,
					binding: missing.binding,
					kind: missing.kind,
				};
			}
			1
		}
		None => 0,
	}
}
