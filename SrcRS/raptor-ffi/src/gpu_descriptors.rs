use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{
	BufferRecord, DescriptorCache, DescriptorEntryRef, DescriptorResourceRef, DescriptorSetRecord,
	ImageRecord, KIND_BUFFER, KIND_IMAGE,
};

use crate::gpu::RxGpuDevice;
use crate::gpu_passes_layouts::RxDsLayoutCache;
use crate::gpu_resources::RxGpuAllocator;

pub type RxDescriptorCache = DescriptorCache;
pub type RxDescriptorSet = DescriptorSetRecord;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxDescriptorEntry
{
	pub binding: u32,
	pub stages: u32,
	pub kind: u32,
	pub sampler: u64,
	pub image: *const ImageRecord,
	pub buffer: *const BufferRecord,
	pub offset: u64,
	pub range: u64,
}

pub(crate) fn entry_ref(entry: &RxDescriptorEntry) -> Option<DescriptorEntryRef>
{
	let resource = match entry.kind {
		KIND_IMAGE => DescriptorResourceRef::Image {
			image: entry.image,
			sampler: vk::Sampler::from_raw(entry.sampler),
		},
		KIND_BUFFER => DescriptorResourceRef::Buffer {
			buffer: entry.buffer,
			offset: entry.offset,
			range: entry.range,
		},
		_ => return None,
	};

	Some(DescriptorEntryRef {
		binding: entry.binding,
		stages: vk::ShaderStageFlags::from_raw(entry.stages),
		resource,
	})
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_descriptor_cache_new() -> *mut RxDescriptorCache
{
	Box::into_raw(Box::new(DescriptorCache::default()))
}

/// # Safety
///
/// `cache` must be null or come from `rx_descriptor_cache_new`, and must not be used afterwards.
/// `device` and `allocator` must be live, or both null to leak what the cache made, and nothing may
/// be using the sets or their resources.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_cache_destroy(
	cache: *mut RxDescriptorCache,
	device: *const RxGpuDevice,
	allocator: *const RxGpuAllocator,
)
{
	if cache.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	let cache = unsafe { *Box::from_raw(cache) };

	// SAFETY: guaranteed by the caller.
	if let (Some(device), Some(allocator)) = unsafe { (device.as_ref(), allocator.as_ref()) } {
		// SAFETY: guaranteed by the caller.
		unsafe { cache.destroy(&device.device, &allocator.0) };
	}
}

/// Finds the set for the entries, making it if there is none.
///
/// # Safety
///
/// `cache`, `layouts` and `device` must be live, `entries` valid for `count` entries whose records
/// are live, and the outputs writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_cache_request(
	cache: *mut RxDescriptorCache,
	layouts: *const RxDsLayoutCache,
	device: *const RxGpuDevice,
	entries: *const RxDescriptorEntry,
	count: usize,
	out_id: *mut u32,
	out_set: *mut *mut RxDescriptorSet,
) -> i32
{
	let entries: Vec<_> = if count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(entries, count) }
			.iter()
			.filter_map(entry_ref)
			.collect()
	};

	// SAFETY: guaranteed by the caller.
	let result = unsafe { (*cache).request(&(*device).device, &(*layouts).0, &entries) };

	match result {
		Ok((id, set)) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*out_id = id;
				*out_set = set;
			}
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `cache` must be live. The result is null if there is no such set.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_cache_find(
	cache: *mut RxDescriptorCache,
	id: u32,
) -> *mut RxDescriptorSet
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *cache }
		.find(id)
		.unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `cache`, `device` and `allocator` must be live, and the set must not be in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_cache_free(
	cache: *mut RxDescriptorCache,
	device: *const RxGpuDevice,
	allocator: *const RxGpuAllocator,
	id: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*cache).free(&(*device).device, &(*allocator).0, id) };
}

/// # Safety
///
/// `cache`, `layouts` and `device` must be live, and no set may be in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_cache_rebuild_all(
	cache: *mut RxDescriptorCache,
	layouts: *const RxDsLayoutCache,
	device: *const RxGpuDevice,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { (*cache).rebuild_all(&(*device).device, &(*layouts).0) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `set` and `device` must be live, `cmd` recording, `layout` the bound pipeline's layout, and
/// `offsets` valid for `offset_count` entries.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_set_bind(
	set: *const RxDescriptorSet,
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	bind_point: i32,
	layout: u64,
	first_set: u32,
	offsets: *const u32,
	offset_count: usize,
)
{
	let offsets = if offset_count == 0 {
		&[][..]
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(offsets, offset_count) }
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		(*set).bind(
			&(*device).device,
			vk::CommandBuffer::from_raw(cmd as u64),
			vk::PipelineBindPoint::from_raw(bind_point),
			vk::PipelineLayout::from_raw(layout),
			first_set,
			offsets,
		)
	};
}
