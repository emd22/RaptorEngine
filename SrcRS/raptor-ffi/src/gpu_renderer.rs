use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{
	AttachmentInfo, ClearTarget, DescriptorSlot, INVALID_INDEX, KeyRegistration, PipelineKey,
	PipelineRegistry, ReflectionEntry, TargetAspect, blend_hash, clear_values,
	filter_by_input_mask, find_format_index, find_missing_descriptor, formats_compatible,
	layout_hash, pass_hash,
};

use crate::gpu::RxGpuDevice;

pub type RxPipelineKey = PipelineKey;

pub struct RxPipelineRegistry(PipelineRegistry);

#[unsafe(no_mangle)]
pub extern "C" fn rx_pipeline_registry_create(
	num_static: u32,
	max_dynamic: u32,
) -> *mut RxPipelineRegistry
{
	Box::into_raw(Box::new(RxPipelineRegistry(PipelineRegistry::new(
		num_static,
		max_dynamic,
	))))
}

/// # Safety
///
/// `registry` must be null or from `rx_pipeline_registry_create`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_free(registry: *mut RxPipelineRegistry)
{
	if !registry.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(registry) });
	}
}

/// # Safety
///
/// `registry` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_allocate(registry: *const RxPipelineRegistry) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &(*registry).0 }
		.allocate()
		.unwrap_or(INVALID_INDEX)
}

/// # Safety
///
/// `registry` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_has_pending(registry: *const RxPipelineRegistry)
-> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &(*registry).0 }.has_pending())
}

/// # Safety
///
/// `registry` must be live and `out` writable for `capacity` entries.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_take_pending(
	registry: *const RxPipelineRegistry,
	out: *mut u32,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let pending = unsafe { &(*registry).0 }.take_pending();
	let count = pending.len().min(capacity);

	// SAFETY: guaranteed by the caller.
	unsafe { std::ptr::copy_nonoverlapping(pending.as_ptr(), out, count) };

	count
}

pub const KEY_REGISTERED: i32 = 0;
pub const KEY_IDENTICAL: i32 = 1;
pub const KEY_COLLISION: i32 = 2;

/// # Safety
///
/// `registry` must be live, `key` valid, and `other` and `hash` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_register_key(
	registry: *const RxPipelineRegistry,
	handle: u32,
	key: *const RxPipelineKey,
	other: *mut u32,
	hash: *mut u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (registry, key) = unsafe { (&(*registry).0, &*key) };

	match registry.register_key(handle, key) {
		KeyRegistration::Registered => KEY_REGISTERED,
		KeyRegistration::IdenticalKey {
			other: found,
			hash: found_hash,
		} => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*other = found;
				*hash = found_hash;
			}
			KEY_IDENTICAL
		}
		KeyRegistration::HashCollision {
			other: found,
			hash: found_hash,
		} => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*other = found;
				*hash = found_hash;
			}
			KEY_COLLISION
		}
	}
}

/// # Safety
///
/// `registry` must be live and `key` valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_find(
	registry: *const RxPipelineRegistry,
	key: *const RxPipelineKey,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*registry).0.find(&*key) }.unwrap_or(INVALID_INDEX)
}

/// # Safety
///
/// `registry` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_get_key(
	registry: *const RxPipelineRegistry,
	handle: u32,
	out: *mut RxPipelineKey,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { &(*registry).0 }.key(handle) {
		Some(key) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = key };
			1
		}
		None => 0,
	}
}

/// # Safety
///
/// `registry` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_register_variant(
	registry: *const RxPipelineRegistry,
	pass: u32,
	features: u32,
	handle: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &(*registry).0 }.register_variant(pass, features, handle);
}

/// # Safety
///
/// `registry` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_find_variant(
	registry: *const RxPipelineRegistry,
	pass: u32,
	features: u32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &(*registry).0 }.find_variant(pass, features)
}

/// # Safety
///
/// `registry` must be live and `out_pass` and `out_features` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_variant_info(
	registry: *const RxPipelineRegistry,
	handle: u32,
	out_pass: *mut u32,
	out_features: *mut u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { &(*registry).0 }.variant_info(handle) {
		Some((pass, features)) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*out_pass = pass;
				*out_features = features;
			}
			1
		}
		None => 0,
	}
}

/// # Safety
///
/// `registry` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_registry_pass_pipeline(
	registry: *const RxPipelineRegistry,
	pass: u32,
	index: usize,
	out: *mut u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { &(*registry).0 }.pass_pipeline(pass, index) {
		Some(handle) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = handle };
			1
		}
		None => 0,
	}
}

/// # Safety
///
/// `key` must be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_pipeline_key_hash(key: *const RxPipelineKey) -> u64
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*key }.hash()
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

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxHashPair
{
	pub first: u32,
	pub second: u32,
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

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxAttachmentInfo
{
	pub format: i32,
	pub samples: u32,
	pub load_op: i32,
	pub store_op: i32,
	pub stencil_load_op: i32,
	pub stencil_store_op: i32,
	pub initial_layout: i32,
	pub final_layout: i32,
}

/// # Safety
///
/// `info` must be valid and `out` writable for one `VkAttachmentDescription`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_attachment_description(info: *const RxAttachmentInfo, out: *mut c_void)
{
	// SAFETY: guaranteed by the caller.
	let info = unsafe { &*info };

	let description = AttachmentInfo {
		format: vk::Format::from_raw(info.format),
		samples: vk::SampleCountFlags::from_raw(info.samples),
		load_op: vk::AttachmentLoadOp::from_raw(info.load_op),
		store_op: vk::AttachmentStoreOp::from_raw(info.store_op),
		stencil_load_op: vk::AttachmentLoadOp::from_raw(info.stencil_load_op),
		stencil_store_op: vk::AttachmentStoreOp::from_raw(info.stencil_store_op),
		initial_layout: vk::ImageLayout::from_raw(info.initial_layout),
		final_layout: vk::ImageLayout::from_raw(info.final_layout),
	}
	.description();

	// SAFETY: guaranteed by the caller.
	unsafe { *out.cast::<vk::AttachmentDescription>() = description };
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxClearTarget
{
	pub aspect: u32,
	pub load_op: i32,
	pub render_pass_only: u8,
}

/// # Safety
///
/// `targets` must be valid for `count` entries and `out` writable for `capacity` `VkClearValue`s.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_clear_values(
	targets: *const RxClearTarget,
	count: usize,
	out: *mut c_void,
	capacity: usize,
) -> usize
{
	let targets: Vec<_> = if count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(targets, count) }
			.iter()
			.map(|target| ClearTarget {
				aspect: match target.aspect {
					1 => Some(TargetAspect::Color),
					2 => Some(TargetAspect::Depth),
					_ => None,
				},
				load_op: vk::AttachmentLoadOp::from_raw(target.load_op),
				render_pass_only: target.render_pass_only != 0,
			})
			.collect()
	};

	let values = clear_values(&targets);
	let written = values.len().min(capacity);

	// SAFETY: guaranteed by the caller.
	unsafe {
		std::ptr::copy_nonoverlapping(values.as_ptr(), out.cast::<vk::ClearValue>(), written)
	};

	written
}

/// # Safety
///
/// `formats` must be valid for `count` entries.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_find_format_index(
	formats: *const u16,
	count: usize,
	format: u16,
	sub_index: i32,
) -> i32
{
	if count == 0 {
		return -1;
	}

	// SAFETY: guaranteed by the caller.
	find_format_index(
		unsafe { std::slice::from_raw_parts(formats, count) },
		format,
		sub_index,
	)
}

/// # Safety
///
/// `a` and `b` must be valid for their counts.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_formats_compatible(
	a: *const u16,
	a_count: usize,
	b: *const u16,
	b_count: usize,
) -> u8
{
	let slice = |data: *const u16, count: usize| -> &[u16] {
		if count == 0 {
			&[]
		} else {
			// SAFETY: guaranteed by the caller.
			unsafe { std::slice::from_raw_parts(data, count) }
		}
	};

	u8::from(formats_compatible(slice(a, a_count), slice(b, b_count)))
}

/// # Safety
///
/// `device` must be live and `cmd` recording with dynamic viewport and scissor state.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_set_viewport_scissor(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	x: i32,
	y: i32,
	width: u32,
	height: u32,
)
{
	let area = vk::Rect2D {
		offset: vk::Offset2D { x, y },
		extent: vk::Extent2D { width, height },
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.cmd_set_viewport_scissor(vk::CommandBuffer::from_raw(cmd as u64), area)
	};
}
