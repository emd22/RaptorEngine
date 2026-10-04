use std::ffi::c_void;
use std::mem::size_of;

use ash::vk::{self, Handle};
use raptor_gpu::{
	AddressMode, BorderColor, CompareOp, Filter, LayoutTransition, SamplerCache, SamplerEntry,
	SamplerProps,
};

use crate::gpu::RxGpuDevice;

const _: () = {
	assert!(size_of::<SamplerEntry>() == size_of::<u64>());
};

fn command_buffer(cmd: *mut c_void) -> vk::CommandBuffer
{
	vk::CommandBuffer::from_raw(cmd as u64)
}

/// # Safety
///
/// `device` must be live, `cmd` recording, and `image` live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_image_layout_transition(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	image: u64,
	aspect: u32,
	old_layout: i32,
	new_layout: i32,
	base_mip: u32,
	levels: u32,
	cmd_queue_family: u32,
)
{
	let transition = LayoutTransition {
		image: vk::Image::from_raw(image),
		aspect: vk::ImageAspectFlags::from_raw(aspect),
		old_layout: vk::ImageLayout::from_raw(old_layout),
		new_layout: vk::ImageLayout::from_raw(new_layout),
		base_mip,
		levels,
		cmd_queue_family,
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.cmd_image_layout_transition(command_buffer(cmd), &transition)
	};
}

/// # Safety
///
/// `device` must be live, `cmd` recording on the transfer queue family, and `image` live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_image_transfer_release(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	image: u64,
	aspect: u32,
	mips: u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let recorded = unsafe {
		(*device).device.cmd_image_transfer_release(
			command_buffer(cmd),
			vk::Image::from_raw(image),
			vk::ImageAspectFlags::from_raw(aspect),
			mips,
		)
	};

	i32::from(recorded)
}

/// # Safety
///
/// `device` must be live, `cmd` recording on the graphics queue family, and `image` live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_image_graphics_acquire(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	image: u64,
	aspect: u32,
	mips: u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let recorded = unsafe {
		(*device).device.cmd_image_graphics_acquire(
			command_buffer(cmd),
			vk::Image::from_raw(image),
			vk::ImageAspectFlags::from_raw(aspect),
			mips,
		)
	};

	i32::from(recorded)
}

/// # Safety
///
/// `device` must be live, `cmd` recording, and `buffer` live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_buffer_compute_to_fragment(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	buffer: u64,
	size: u64,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.cmd_buffer_compute_to_fragment(
			command_buffer(cmd),
			vk::Buffer::from_raw(buffer),
			size,
		)
	};
}

/// # Safety
///
/// `device` must be live, `cmd` recording, and `buffer` live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_buffer_fragment_to_compute(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	buffer: u64,
	size: u64,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.cmd_buffer_fragment_to_compute(
			command_buffer(cmd),
			vk::Buffer::from_raw(buffer),
			size,
		)
	};
}

pub struct RxSamplerCache(SamplerCache);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxSamplerProps
{
	pub min_filter: u8,
	pub mag_filter: u8,
	pub mip_filter: u8,
	pub address_mode: u8,
	pub border_color: u8,
	pub compare_op: u8,
	pub max_anisotropy: u8,
	pub min_lod: f32,
	pub max_lod: f32,
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_gpu_sampler_cache_create() -> *mut RxSamplerCache
{
	Box::into_raw(Box::new(RxSamplerCache(SamplerCache::default())))
}

/// # Safety
///
/// `cache` and `device` must be live and `props` valid. The returned pointer is valid until the
/// cache is freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_sampler_cache_request(
	cache: *const RxSamplerCache,
	device: *const RxGpuDevice,
	props: *const RxSamplerProps,
) -> *const c_void
{
	// SAFETY: guaranteed by the caller.
	let (cache, device, props) = unsafe { (&(*cache).0, &(*device).device, &*props) };

	let props = SamplerProps {
		min_filter: Filter::from_raw(props.min_filter),
		mag_filter: Filter::from_raw(props.mag_filter),
		mip_filter: Filter::from_raw(props.mip_filter),
		address_mode: AddressMode::from_raw(props.address_mode),
		border_color: BorderColor::from_raw(props.border_color),
		compare_op: CompareOp::from_raw(props.compare_op),
		max_anisotropy: props.max_anisotropy,
		min_lod: props.min_lod,
		max_lod: props.max_lod,
	};

	cache.request(device, &props).cast()
}

/// # Safety
///
/// `cache` must be null or come from `rx_gpu_sampler_cache_create`, and `device` must be the live
/// device its samplers were created on. No sampler may be in use, and the cache must not be used
/// afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_sampler_cache_free(
	cache: *mut RxSamplerCache,
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
