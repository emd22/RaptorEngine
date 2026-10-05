use ash::vk::{self, Handle};
use raptor_gpu::{ImageFormat, Level, SwapchainRequest};

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
