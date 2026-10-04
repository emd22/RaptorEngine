use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{BufferType, DescriptorWrite, ImageFormat, Level, SwapchainRequest};

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
/// `device` must be live, `swapchain` and `semaphore` handles of it, and `out_index` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_swapchain_acquire(
	device: *const RxGpuDevice,
	swapchain: u64,
	timeout: u64,
	semaphore: u64,
	out_index: *mut u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	let (result, index) = device.acquire_next_image(
		vk::SwapchainKHR::from_raw(swapchain),
		timeout,
		vk::Semaphore::from_raw(semaphore),
	);

	// SAFETY: guaranteed by the caller.
	unsafe { *out_index = index };

	result.as_raw()
}

/// # Safety
///
/// `device` must be live, `queue` a presentation queue of it that the caller has locked, and the
/// other handles its own.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_swapchain_present(
	device: *const RxGpuDevice,
	queue: *mut c_void,
	swapchain: u64,
	wait_semaphore: u64,
	image_index: u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let result = unsafe {
		(*device).device.queue_present(
			vk::Queue::from_raw(queue as u64),
			vk::SwapchainKHR::from_raw(swapchain),
			vk::Semaphore::from_raw(wait_semaphore),
			image_index,
		)
	};

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

/// # Safety
///
/// `device` must be live, `sizes` valid for `count` entries, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_descriptor_pool_create(
	device: *const RxGpuDevice,
	sizes: *const RxDescriptorPoolSize,
	count: usize,
	max_sets: u32,
	free_sets: u8,
	out: *mut u64,
) -> i32
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
	match unsafe { &(*device).device }.create_descriptor_pool(&sizes, max_sets, free_sets != 0) {
		Ok(pool) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = pool.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `pool` one of its pools with no sets in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_descriptor_pool_destroy(device: *const RxGpuDevice, pool: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_descriptor_pool(vk::DescriptorPool::from_raw(pool))
	};
}

/// # Safety
///
/// `device` must be live, `pool` and `layout` handles of it, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_descriptor_set_allocate(
	device: *const RxGpuDevice,
	pool: u64,
	layout: u64,
	out: *mut u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	match device.allocate_descriptor_set(
		vk::DescriptorPool::from_raw(pool),
		vk::DescriptorSetLayout::from_raw(layout),
	) {
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
/// `device` must be live, `pool` created with free sets, and `set` allocated from it and not in
/// use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_descriptor_set_free(device: *const RxGpuDevice, pool: u64, set: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.free_descriptor_set(
			vk::DescriptorPool::from_raw(pool),
			vk::DescriptorSet::from_raw(set),
		)
	};
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
