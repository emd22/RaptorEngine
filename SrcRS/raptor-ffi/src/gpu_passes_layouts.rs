use ash::vk::{self, Handle};
use raptor_gpu::{DsLayoutCache, DsLayoutEntry, layout_id};

use crate::gpu::RxGpuDevice;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxDsLayoutEntry
{
	pub binding: u32,
	pub descriptor_type: i32,
	pub stages: u32,
	pub count: u32,
}

fn entries(raw: *const RxDsLayoutEntry, count: usize) -> Vec<DsLayoutEntry>
{
	if count == 0 {
		return Vec::new();
	}

	// SAFETY: the callers of the exported functions guarantee `raw` is valid for `count` entries.
	unsafe { std::slice::from_raw_parts(raw, count) }
		.iter()
		.map(|entry| DsLayoutEntry {
			binding: entry.binding,
			descriptor_type: vk::DescriptorType::from_raw(entry.descriptor_type),
			stages: vk::ShaderStageFlags::from_raw(entry.stages),
			count: entry.count,
		})
		.collect()
}

pub struct RxDsLayoutCache(pub(crate) DsLayoutCache);

/// # Safety
///
/// `device` must be live, `entries_in` valid for `count` entries, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_ds_layout_create(
	device: *const RxGpuDevice,
	entries_in: *const RxDsLayoutEntry,
	count: usize,
	out: *mut u64,
) -> i32
{
	let entries = entries(entries_in, count);

	// SAFETY: guaranteed by the caller.
	match unsafe { &(*device).device }.create_ds_layout(&entries) {
		Ok(layout) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = layout.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `entries_in` must be valid for `count` entries.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_ds_layout_id(
	entries_in: *const RxDsLayoutEntry,
	count: usize,
) -> u32
{
	layout_id(&entries(entries_in, count))
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_gpu_ds_layout_cache_create() -> *mut RxDsLayoutCache
{
	Box::into_raw(Box::new(RxDsLayoutCache(DsLayoutCache::default())))
}

/// # Safety
///
/// `cache` and `device` must be live, `entries_in` valid for `count` entries, and the outputs
/// writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_ds_layout_cache_request(
	cache: *const RxDsLayoutCache,
	device: *const RxGpuDevice,
	entries_in: *const RxDsLayoutEntry,
	count: usize,
	out_id: *mut u32,
	out_layout: *mut u64,
) -> i32
{
	let entries = entries(entries_in, count);

	// SAFETY: guaranteed by the caller.
	let (cache, device) = unsafe { (&(*cache).0, &(*device).device) };

	match cache.request(device, &entries) {
		Ok((id, layout)) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*out_id = id;
				*out_layout = layout.as_raw();
			}
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `cache` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_ds_layout_cache_get(cache: *const RxDsLayoutCache, id: u32) -> u64
{
	// SAFETY: guaranteed by the caller.
	unsafe { &(*cache).0 }
		.get(id)
		.map_or(0, |layout| layout.as_raw())
}

/// # Safety
///
/// `cache` and `device` must be live, and the layout must not be in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_ds_layout_cache_free(
	cache: *const RxDsLayoutCache,
	device: *const RxGpuDevice,
	id: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*cache).0.free(&(*device).device, id) };
}

/// # Safety
///
/// `cache` must be null or from `rx_gpu_ds_layout_cache_create`, `device` null or the live device
/// its layouts were created on, and no layout may be in use. The cache must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_ds_layout_cache_destroy(
	cache: *mut RxDsLayoutCache,
	device: *const RxGpuDevice,
)
{
	if cache.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe {
		let cache = Box::from_raw(cache);

		if !device.is_null() {
			cache.0.destroy(&(*device).device);
		}
	}
}
