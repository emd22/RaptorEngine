use std::ffi::{c_char, c_void};

use ash::vk::{self, Handle};
use raptor_gpu::{BufferType, CopyError, ImageFormat, ImageType, Memory, MipChain, mip_dimensions};

use crate::gpu::RxGpuDevice;

fn format(raw: u16) -> ImageFormat
{
	ImageFormat::from_raw(raw).unwrap_or(ImageFormat::None)
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_image_format_to_vk(raw: u16) -> i32
{
	format(raw).to_vk().as_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_image_format_pixel_stride(raw: u16) -> u32
{
	format(raw).pixel_stride()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_image_format_aspect_mask(raw: u16) -> u32
{
	format(raw).aspect_mask().as_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_image_format_usage(raw: u16) -> u32
{
	format(raw).usage_flags().as_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_image_format_is_depth(raw: u16) -> u8
{
	u8::from(format(raw).is_depth())
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_image_format_is_stencil(raw: u16) -> u8
{
	u8::from(format(raw).is_stencil())
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_image_format_is_srgb(raw: u16) -> u8
{
	u8::from(format(raw).is_srgb())
}

/// # Safety
///
/// `view_type` and `layer_count` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_type_properties(
	image_type: u32,
	cube_count: u32,
	view_type: *mut i32,
	layer_count: *mut u32,
) -> i32
{
	let Some(image_type) = ImageType::from_raw(image_type) else {
		return 0;
	};

	let (view, layers) = image_type.properties(cube_count);

	// SAFETY: guaranteed by the caller.
	unsafe {
		*view_type = view.as_raw();
		*layer_count = layers;
	}

	1
}

/// # Safety
///
/// `out_width` and `out_height` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_image_mip_dimensions(
	width: u32,
	height: u32,
	level: u32,
	out_width: *mut u32,
	out_height: *mut u32,
)
{
	let (w, h) = mip_dimensions((width, height), level);

	// SAFETY: guaranteed by the caller.
	unsafe {
		*out_width = w;
		*out_height = h;
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_buffer_type_usage(raw: u32) -> u32
{
	BufferType::from_raw(raw)
		.unwrap_or(BufferType::None)
		.usage()
		.as_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_buffer_type_descriptor_type(raw: u32) -> i32
{
	BufferType::from_raw(raw)
		.unwrap_or(BufferType::None)
		.descriptor_type()
		.as_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_buffer_type_name(raw: u32) -> *const c_char
{
	BufferType::from_raw(raw)
		.map_or(c"", BufferType::name)
		.as_ptr()
}

pub(crate) fn memory(raw: u32) -> Memory
{
	match raw {
		1 => Memory::AutoPreferDevice,
		2 => Memory::GpuOnly,
		3 => Memory::CpuOnly,
		4 => Memory::CpuToGpu,
		5 => Memory::GpuToCpu,
		_ => Memory::Auto,
	}
}

/// # Safety
///
/// `device` must be live, `cmd` a recording command buffer, and both buffers live with transfer
/// usage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_copy_buffer(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	src: u64,
	dst: u64,
	size: u64,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.cmd_copy_buffer(
			vk::CommandBuffer::from_raw(cmd as u64),
			vk::Buffer::from_raw(src),
			vk::Buffer::from_raw(dst),
			size,
		)
	};
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxImageDesc
{
	pub image_type: u32,
	pub width: u32,
	pub height: u32,
	pub mips: u32,
	pub format: u32,
	pub tiling: i32,
	pub usage: u32,
	pub aspect: u32,
	pub cube_count: u32,
	pub initial_layout: i32,
	pub is_target: u8,
}

/// # Safety
///
/// `device` must be live, `cmd` recording, the image in `TRANSFER_DST_OPTIMAL`, and the buffer must
/// hold the chain.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_copy_buffer_to_mips(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	buffer: u64,
	image: u64,
	format: u16,
	width: u32,
	height: u32,
	mips: u32,
	aspect: u32,
) -> i32
{
	let chain = MipChain {
		format: self::format(format),
		size: (width, height),
		mips,
		aspect: vk::ImageAspectFlags::from_raw(aspect),
	};

	// SAFETY: guaranteed by the caller.
	let result = unsafe {
		(*device).device.cmd_copy_buffer_to_mips(
			vk::CommandBuffer::from_raw(cmd as u64),
			vk::Buffer::from_raw(buffer),
			vk::Image::from_raw(image),
			&chain,
		)
	};

	match result {
		Ok(()) => 0,
		Err(CopyError::Misaligned) => 1,
	}
}

/// # Safety
///
/// `device` must be live, `cmd` recording, and the image in `TRANSFER_DST_OPTIMAL`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_copy_buffer_to_region(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	buffer: u64,
	image: u64,
	mip: u32,
	width: u32,
	height: u32,
	offset_x: i32,
	offset_y: i32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.cmd_copy_buffer_to_region(
			vk::CommandBuffer::from_raw(cmd as u64),
			vk::Buffer::from_raw(buffer),
			vk::Image::from_raw(image),
			mip,
			(width, height),
			(offset_x, offset_y),
		)
	};
}
