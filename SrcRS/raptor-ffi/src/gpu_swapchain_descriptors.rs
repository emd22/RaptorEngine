use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{
	BufferType, DescriptorIdEntry, DescriptorPoolRecord, DescriptorWrite, ImageFormat, Level,
	SwapchainRequest, descriptor_id,
};

use crate::gpu::RxGpuDevice;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxSwapchainResult
{
	pub handle: u64,
	pub format: u16,
	pub color_space: i32,
}

/// # Safety
///
/// `device` must be live, `surface` a surface of its instance, `old` null or a swapchain of the
/// device, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_swapchain_create(
	device: *const RxGpuDevice,
	surface: u64,
	width: u32,
	height: u32,
	old: u64,
	out: *mut RxSwapchainResult,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	let request = SwapchainRequest {
		surface: vk::SurfaceKHR::from_raw(surface),
		size: (width, height),
		old: vk::SwapchainKHR::from_raw(old),
	};

	match device.create_swapchain(&request) {
		Ok(info) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*out = RxSwapchainResult {
					handle: info.handle.as_raw(),
					format: info.format as u16,
					color_space: info.color_space.as_raw(),
				};
			}
			1
		}
		Err(error) => {
			device.log().log(Level::Error, &error.to_string());
			0
		}
	}
}

/// # Safety
///
/// `device` must be live and `swapchain` one of its swapchains. If `out` is not null it must be
/// writable for `capacity` images.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_swapchain_images(
	device: *const RxGpuDevice,
	swapchain: u64,
	out: *mut u64,
	capacity: u32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	let Ok(images) = device.swapchain_images(vk::SwapchainKHR::from_raw(swapchain)) else {
		return 0;
	};

	if !out.is_null() {
		for (index, image) in images.iter().take(capacity as usize).enumerate() {
			// SAFETY: guaranteed by the caller.
			unsafe { *out.add(index) = image.as_raw() };
		}
	}

	images.len() as u32
}

/// # Safety
///
/// `device` must be live and `swapchain` one of its swapchains with no images in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_swapchain_destroy(device: *const RxGpuDevice, swapchain: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_swapchain(vk::SwapchainKHR::from_raw(swapchain))
	};
}

/// # Safety
///
/// `device` must be live and the other handles its own.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_swapchain_present(
	device: *const RxGpuDevice,
	swapchain: u64,
	wait_semaphore: u64,
	image_index: u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let result = unsafe { &*device }.device.queue_present(
		vk::SwapchainKHR::from_raw(swapchain),
		vk::Semaphore::from_raw(wait_semaphore),
		image_index,
	);

	result.as_raw()
}

/// # Safety
///
/// `device` must be live, `image` one of its images, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_color_view_create(
	device: *const RxGpuDevice,
	image: u64,
	format: u16,
	out: *mut u64,
) -> i32
{
	let format = ImageFormat::from_raw(format).unwrap_or(ImageFormat::None);

	// SAFETY: guaranteed by the caller.
	match unsafe { &(*device).device }.create_color_view(vk::Image::from_raw(image), format) {
		Ok(view) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = view.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `view` one of its image views that is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_view_destroy(device: *const RxGpuDevice, view: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*device).device.destroy_view(vk::ImageView::from_raw(view)) };
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxDescriptorPoolSize
{
	pub descriptor_type: i32,
	pub count: u32,
}

pub type RxDescriptorPool = DescriptorPoolRecord;

/// # Safety
///
/// `device` must be live, `sizes` valid for `count` entries, and `out_status` writable. The result
/// is null on failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_pool_new(
	device: *const RxGpuDevice,
	sizes: *const RxDescriptorPoolSize,
	count: usize,
	max_sets: u32,
	free_sets: u8,
	out_status: *mut i32,
) -> *mut RxDescriptorPool
{
	let sizes: Vec<_> = if count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(sizes, count) }
			.iter()
			.map(|size| vk::DescriptorPoolSize {
				ty: vk::DescriptorType::from_raw(size.descriptor_type),
				descriptor_count: size.count,
			})
			.collect()
	};

	// SAFETY: guaranteed by the caller.
	let created = DescriptorPoolRecord::create(
		unsafe { &(*device).device },
		sizes,
		max_sets,
		free_sets != 0,
	);

	let status = match &created {
		Ok(_) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	};

	// SAFETY: guaranteed by the caller.
	unsafe { *out_status = status };

	created.map_or(std::ptr::null_mut(), Box::into_raw)
}

/// # Safety
///
/// `pool` must come from `rx_descriptor_pool_new` on this device with none of its sets in use.
/// Every set allocated from it is invalid afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_pool_recreate(
	pool: *mut RxDescriptorPool,
	device: *const RxGpuDevice,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { (*pool).recreate(&(*device).device) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `pool` must be null or come from `rx_descriptor_pool_new` on this device, with none of its sets
/// in use, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_pool_destroy(
	pool: *mut RxDescriptorPool,
	device: *const RxGpuDevice,
)
{
	if pool.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe { Box::from_raw(pool).destroy(&(*device).device) };
}

/// # Safety
///
/// `pool` and `device` must be live, `layout` a handle of the device, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_pool_allocate_set(
	pool: *mut RxDescriptorPool,
	device: *const RxGpuDevice,
	layout: u64,
	out: *mut u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let allocated = unsafe {
		(*pool).allocate_set(&(*device).device, vk::DescriptorSetLayout::from_raw(layout))
	};

	match allocated {
		Ok(set) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = set.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `pool` must have been created with free sets, and `set` allocated from it and not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_pool_free_set(
	pool: *const RxDescriptorPool,
	device: *const RxGpuDevice,
	set: u64,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*pool).free_set(&(*device).device, vk::DescriptorSet::from_raw(set)) };
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxDescriptorIdEntry
{
	pub binding: u32,
	pub kind: u32,
	pub handle: u64,
}

/// # Safety
///
/// `entries` must be valid for `count` entries.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_descriptor_id(entries: *const RxDescriptorIdEntry, count: usize)
-> u32
{
	let entries: Vec<_> = if count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(entries, count) }
			.iter()
			.map(|entry| DescriptorIdEntry {
				binding: entry.binding,
				kind: entry.kind,
				handle: entry.handle,
			})
			.collect()
	};

	descriptor_id(&entries)
}

pub const DESCRIPTOR_KIND_IMAGE: u32 = 1;
pub const DESCRIPTOR_KIND_BUFFER: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxDescriptorWrite
{
	pub binding: u32,
	pub kind: u32,
	pub sampler: u64,
	pub view: u64,
	pub buffer: u64,
	pub offset: u64,
	pub range: u64,
	pub buffer_type: u32,
}

/// # Safety
///
/// `device` must be live, `set` one of its sets, `writes` valid for `count` entries, and every
/// handle in them live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_descriptor_set_update(
	device: *const RxGpuDevice,
	set: u64,
	writes: *const RxDescriptorWrite,
	count: usize,
) -> i32
{
	if count == 0 {
		return 1;
	}

	// SAFETY: guaranteed by the caller.
	let raw = unsafe { std::slice::from_raw_parts(writes, count) };

	let mut converted = Vec::with_capacity(count);

	for write in raw {
		converted.push(match write.kind {
			DESCRIPTOR_KIND_IMAGE => DescriptorWrite::Image {
				binding: write.binding,
				sampler: vk::Sampler::from_raw(write.sampler),
				view: vk::ImageView::from_raw(write.view),
			},
			DESCRIPTOR_KIND_BUFFER => {
				let Some(buffer_type) = BufferType::from_raw(write.buffer_type) else {
					return 0;
				};

				DescriptorWrite::Buffer {
					binding: write.binding,
					buffer: vk::Buffer::from_raw(write.buffer),
					offset: write.offset,
					range: write.range,
					buffer_type,
				}
			}
			_ => return 0,
		});
	}

	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.update_descriptor_set(vk::DescriptorSet::from_raw(set), &converted)
	};

	1
}

/// # Safety
///
/// `device` must be live, `cmd` recording, `layout` the bound pipeline's layout, and `sets` /
/// `offsets` valid for their counts.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_bind_descriptor_sets(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	bind_point: i32,
	layout: u64,
	first_set: u32,
	sets: *const u64,
	set_count: usize,
	offsets: *const u32,
	offset_count: usize,
)
{
	// SAFETY: guaranteed by the caller.
	let (sets, offsets) = unsafe {
		(
			if set_count == 0 {
				&[][..]
			} else {
				std::slice::from_raw_parts(sets, set_count)
			},
			if offset_count == 0 {
				&[][..]
			} else {
				std::slice::from_raw_parts(offsets, offset_count)
			},
		)
	};

	let sets: Vec<_> = sets
		.iter()
		.map(|set| vk::DescriptorSet::from_raw(*set))
		.collect();

	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.cmd_bind_descriptor_sets(
			vk::CommandBuffer::from_raw(cmd as u64),
			vk::PipelineBindPoint::from_raw(bind_point),
			vk::PipelineLayout::from_raw(layout),
			first_set,
			&sets,
			offsets,
		)
	};
}
