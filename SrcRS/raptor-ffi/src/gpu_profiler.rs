use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{GpuProfiler, MARKER_COUNT};

use crate::gpu::RxGpuDevice;

pub type RxGpuProfiler = GpuProfiler;

#[unsafe(no_mangle)]
pub extern "C" fn rx_gpu_marker_count() -> u32
{
	MARKER_COUNT as u32
}

/// # Safety
///
/// `device` must be live. The result is null if the GPU can not take timestamps.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_profiler_new(
	device: *const RxGpuDevice,
	graphics_family: u32,
	frames_in_flight: u32,
) -> *mut RxGpuProfiler
{
	// SAFETY: guaranteed by the caller.
	GpuProfiler::create(
		unsafe { &(*device).device },
		graphics_family,
		frames_in_flight,
	)
	.map_or(std::ptr::null_mut(), Box::into_raw)
}

/// # Safety
///
/// `profiler` must be null or come from `rx_gpu_profiler_new`, with nothing using its query pools,
/// and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_profiler_destroy(
	profiler: *mut RxGpuProfiler,
	device: *const RxGpuDevice,
)
{
	if profiler.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe { Box::from_raw(profiler).destroy(&(*device).device) };
}

/// # Safety
///
/// `profiler` and `device` must be live, and the frame slot's fence waited on.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_profiler_read_results(
	profiler: *mut RxGpuProfiler,
	device: *const RxGpuDevice,
	frame_index: u32,
	delta_seconds: f64,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *profiler }.read_results(
		unsafe { &(*device).device },
		frame_index as usize,
		delta_seconds,
	);
}

/// # Safety
///
/// `profiler` and `device` must be live, `cmd` recording outside a render pass, and the frame
/// slot's queries not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_profiler_begin_frame(
	profiler: *mut RxGpuProfiler,
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	frame_index: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*profiler).begin_frame(
			&(*device).device,
			vk::CommandBuffer::from_raw(cmd as u64),
			frame_index as usize,
		)
	};
}

/// # Safety
///
/// `profiler` and `device` must be live and `cmd` recording, in a frame that was begun.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_profiler_mark(
	profiler: *mut RxGpuProfiler,
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	marker: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*profiler).mark(
			&(*device).device,
			vk::CommandBuffer::from_raw(cmd as u64),
			marker as usize,
		)
	};
}

/// # Safety
///
/// `profiler` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_profiler_average_ms(
	profiler: *const RxGpuProfiler,
	marker: u32,
) -> f64
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*profiler }.average_ms(marker as usize)
}

/// # Safety
///
/// `profiler` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_profiler_total_ms(profiler: *const RxGpuProfiler) -> f64
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*profiler }.total_ms()
}
