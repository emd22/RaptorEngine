use ash::vk::{self, Handle};
use raptor_gpu::{ImageDesc, ImageFormat, ImageRecord, ImageType};

use crate::gpu::RxGpuDevice;
use crate::gpu_resources::RxGpuAllocator;
use crate::gpu_resources_typed::RxImageDesc;

pub type RxImage = ImageRecord;

#[unsafe(no_mangle)]
pub extern "C" fn rx_image_new() -> *mut RxImage
{
	ImageRecord::into_raw(ImageRecord::new())
}

/// # Safety
///
/// `record` must come from `rx_image_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_retain(record: *mut RxImage)
{
	// SAFETY: guaranteed by the caller.
	unsafe { ImageRecord::retain(record) };
}

/// # Safety
///
/// `record` must come from `rx_image_new` and not be used by this reference afterwards. `device`
/// and `allocator` must be live, or both null to leak the resources, and the image must not be in
/// use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_release(
	record: *mut RxImage,
	device: *const RxGpuDevice,
	allocator: *const RxGpuAllocator,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		ImageRecord::release(
			record,
			device.as_ref().map(|device| &device.device),
			allocator.as_ref().map(|allocator| &allocator.0),
		)
	};
}

/// # Safety
///
/// `record` must come from `rx_image_new`, `device` and `allocator` must be live and belong
/// together, and `desc` must be valid. Resources the record already holds must not be in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_create(
	record: *mut RxImage,
	device: *const RxGpuDevice,
	allocator: *const RxGpuAllocator,
	desc: *const RxImageDesc,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (record, device, allocator, desc) =
		unsafe { (&*record, &(*device).device, &(*allocator).0, &*desc) };

	let (Some(image_type), Ok(format)) = (
		ImageType::from_raw(desc.image_type),
		u16::try_from(desc.format),
	) else {
		return vk::Result::ERROR_INITIALIZATION_FAILED.as_raw();
	};

	let desc = ImageDesc {
		image_type,
		size: (desc.width, desc.height),
		mips: desc.mips,
		format: ImageFormat::from_raw(format).unwrap_or(ImageFormat::None),
		tiling: vk::ImageTiling::from_raw(desc.tiling),
		usage: vk::ImageUsageFlags::from_raw(desc.usage),
		aspect: vk::ImageAspectFlags::from_raw(desc.aspect),
		is_target: desc.is_target != 0,
		cube_count: desc.cube_count,
		initial_layout: vk::ImageLayout::from_raw(desc.initial_layout),
	};

	// SAFETY: guaranteed by the caller.
	match unsafe { record.create(device, allocator, &desc) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `record` must come from `rx_image_new`. The image is not destroyed with the record.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_wrap_external(
	record: *mut RxImage,
	image: u64,
	width: u32,
	height: u32,
	format: u16,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*record).wrap_external(
			vk::Image::from_raw(image),
			(width, height),
			ImageFormat::from_raw(format).unwrap_or(ImageFormat::None),
		)
	};
}

/// # Safety
///
/// `record` must come from `rx_image_new`, `device` must be live and own the image, and the old
/// view must not be in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_recreate_color_view(
	record: *mut RxImage,
	device: *const RxGpuDevice,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { (*record).recreate_color_view(&(*device).device) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}
