use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{Attachment, DsLayoutCache, DsLayoutEntry, layout_id};

use crate::gpu::RxGpuDevice;

/// # Safety
///
/// `descriptions` must point to `count` `VkAttachmentDescription`s and `is_depth` to `count` bytes.
/// `device` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_render_pass_create(
	device: *const RxGpuDevice,
	descriptions: *const c_void,
	is_depth: *const u8,
	count: usize,
	out: *mut u64,
) -> i32
{
	let attachments: Vec<_> = if count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		let (descriptions, is_depth) = unsafe {
			(
				std::slice::from_raw_parts(descriptions.cast::<vk::AttachmentDescription>(), count),
				std::slice::from_raw_parts(is_depth, count),
			)
		};

		descriptions
			.iter()
			.zip(is_depth)
			.map(|(description, depth)| Attachment {
				description: *description,
				is_depth: *depth != 0,
			})
			.collect()
	};

	// SAFETY: guaranteed by the caller.
	match unsafe { &(*device).device }.create_render_pass(&attachments) {
		Ok(pass) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = pass.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `pass` one of its render passes that is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_render_pass_destroy(device: *const RxGpuDevice, pass: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_render_pass(vk::RenderPass::from_raw(pass))
	};
}

/// # Safety
///
/// `device` must be live, `cmd` recording outside a render pass, `clear_values` valid for
/// `clear_count` `VkClearValue`s, and the pass and framebuffer compatible handles of the device.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_begin_render_pass(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	pass: u64,
	framebuffer: u64,
	x: i32,
	y: i32,
	width: u32,
	height: u32,
	clear_values: *const c_void,
	clear_count: usize,
)
{
	let clear_values = if clear_count == 0 {
		&[][..]
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(clear_values.cast::<vk::ClearValue>(), clear_count) }
	};

	let area = vk::Rect2D {
		offset: vk::Offset2D { x, y },
		extent: vk::Extent2D { width, height },
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.cmd_begin_render_pass(
			vk::CommandBuffer::from_raw(cmd as u64),
			vk::RenderPass::from_raw(pass),
			vk::Framebuffer::from_raw(framebuffer),
			area,
			clear_values,
		)
	};
}

/// # Safety
///
/// `device` must be live and `cmd` inside a render pass.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_end_render_pass(device: *const RxGpuDevice, cmd: *mut c_void)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.cmd_end_render_pass(vk::CommandBuffer::from_raw(cmd as u64))
	};
}

/// # Safety
///
/// `device` must be live, `views` valid for `count` image view handles of it, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_framebuffer_create(
	device: *const RxGpuDevice,
	pass: u64,
	views: *const u64,
	count: usize,
	width: u32,
	height: u32,
	out: *mut u64,
) -> i32
{
	let views: Vec<_> = if count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(views, count) }
			.iter()
			.map(|view| vk::ImageView::from_raw(*view))
			.collect()
	};

	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	match device.create_framebuffer(vk::RenderPass::from_raw(pass), &views, (width, height)) {
		Ok(framebuffer) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = framebuffer.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `framebuffer` one of its framebuffers that is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_framebuffer_destroy(device: *const RxGpuDevice, framebuffer: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_framebuffer(vk::Framebuffer::from_raw(framebuffer))
	};
}

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

pub struct RxDsLayoutCache(DsLayoutCache);

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
