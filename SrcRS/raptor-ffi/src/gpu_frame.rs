use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{FrameLoop, QueueKind, SubmitSignal, SubmitWait};

use crate::gpu::RxGpuDevice;

pub type RxFrameLoop = FrameLoop;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxSubmitWait
{
	pub semaphore: u64,
	pub stages: u32,
	pub value: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxSubmitSignal
{
	pub semaphore: u64,
	pub value: u64,
}

unsafe fn slice_of<'a, T>(data: *const T, count: usize) -> &'a [T]
{
	if count == 0 {
		&[]
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(data, count) }
	}
}

/// # Safety
///
/// `device` must be live, every pointer valid for its count, and every handle one of the device's.
/// The command buffers must be recorded and not pending, and `fence` null or unsignaled.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_queue_submit(
	device: *const RxGpuDevice,
	queue: u32,
	waits: *const RxSubmitWait,
	wait_count: usize,
	commands: *const *mut c_void,
	command_count: usize,
	signals: *const RxSubmitSignal,
	signal_count: usize,
	fence: u64,
) -> i32
{
	let Some(kind) = QueueKind::from_raw(queue) else {
		return vk::Result::ERROR_INITIALIZATION_FAILED.as_raw();
	};

	// SAFETY: guaranteed by the caller.
	let (waits, commands, signals) = unsafe {
		(
			slice_of(waits, wait_count)
				.iter()
				.map(|wait| SubmitWait {
					semaphore: vk::Semaphore::from_raw(wait.semaphore),
					stages: vk::PipelineStageFlags::from_raw(wait.stages),
					value: wait.value,
				})
				.collect::<Vec<_>>(),
			slice_of(commands, command_count)
				.iter()
				.map(|command| vk::CommandBuffer::from_raw(*command as u64))
				.collect::<Vec<_>>(),
			slice_of(signals, signal_count)
				.iter()
				.map(|signal| SubmitSignal {
					semaphore: vk::Semaphore::from_raw(signal.semaphore),
					value: signal.value,
				})
				.collect::<Vec<_>>(),
		)
	};

	// SAFETY: guaranteed by the caller.
	let result = unsafe {
		(*device).device.queue_submit(
			kind,
			&waits,
			&commands,
			&signals,
			vk::Fence::from_raw(fence),
		)
	};

	match result {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_queue_wait_idle(device: *const RxGpuDevice, queue: u32) -> i32
{
	let Some(kind) = QueueKind::from_raw(queue) else {
		return vk::Result::ERROR_INITIALIZATION_FAILED.as_raw();
	};

	// SAFETY: guaranteed by the caller.
	match unsafe { &*device }.device.queue_wait_idle(kind) {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `out_status` writable. The result is null on failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_frame_loop_new(
	device: *const RxGpuDevice,
	frames_in_flight: u32,
	image_count: u32,
	out_status: *mut i32,
) -> *mut RxFrameLoop
{
	// SAFETY: guaranteed by the caller.
	let created = FrameLoop::create(unsafe { &(*device).device }, frames_in_flight, image_count);

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
/// `frame_loop` and `device` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_frame_loop_begin(
	frame_loop: *const RxFrameLoop,
	device: *const RxGpuDevice,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { (*frame_loop).begin_frame(&(*device).device) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `frame_loop` and `device` must be live and `swapchain` a handle of the device.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_frame_loop_acquire(
	frame_loop: *const RxFrameLoop,
	device: *const RxGpuDevice,
	swapchain: u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*frame_loop).acquire(&(*device).device, vk::SwapchainKHR::from_raw(swapchain)) }
		.as_raw()
}

/// # Safety
///
/// `frame_loop` and `device` must be live, `commands` a recorded command buffer that is not
/// pending, `transfer` a timeline semaphore of the device, and `out_present` writable. Returns the
/// submit status, and the present status in `out_present` when the submit worked.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_frame_loop_submit_and_present(
	frame_loop: *const RxFrameLoop,
	device: *const RxGpuDevice,
	swapchain: u64,
	commands: *mut c_void,
	transfer: u64,
	transfer_value: u64,
	out_present: *mut i32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let result = unsafe {
		(*frame_loop).submit_and_present(
			&(*device).device,
			vk::SwapchainKHR::from_raw(swapchain),
			vk::CommandBuffer::from_raw(commands as u64),
			vk::Semaphore::from_raw(transfer),
			transfer_value,
		)
	};

	match result {
		Ok(present) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_present = present.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `frame_loop` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_frame_loop_end_frame(frame_loop: *const RxFrameLoop)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*frame_loop }.end_frame();
}

/// # Safety
///
/// `frame_loop` must be null or come from `rx_frame_loop_new`, with the device idle, and must not
/// be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_frame_loop_destroy(
	frame_loop: *mut RxFrameLoop,
	device: *const RxGpuDevice,
)
{
	if frame_loop.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe { Box::from_raw(frame_loop).destroy(&(*device).device) };
}
