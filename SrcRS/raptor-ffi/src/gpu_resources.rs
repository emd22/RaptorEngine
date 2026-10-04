use std::ffi::c_void;

use ash::vk::{self, Handle};
use raptor_gpu::{Allocation, Allocator, Level, SemaphoreKind};

use crate::gpu::{RxGpuDevice, RxGpuInstance};

pub struct RxGpuAllocator(pub(crate) Allocator);
pub struct RxGpuAllocation(pub(crate) Allocation);

fn code(result: Result<(), vk::Result>) -> i32
{
	match result {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `instance` and `device` must come from their `rx_gpu_*_create` functions, and outlive the
/// allocator.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_allocator_create(
	instance: *const RxGpuInstance,
	device: *const RxGpuDevice,
) -> *mut RxGpuAllocator
{
	// SAFETY: guaranteed by the caller.
	let (instance, device) = unsafe { (&(*instance).0, &(*device).device) };

	// SAFETY: guaranteed by the caller.
	match unsafe { Allocator::new(instance, device) } {
		Ok(allocator) => Box::into_raw(Box::new(RxGpuAllocator(allocator))),
		Err(error) => {
			instance.log().log(
				Level::Error,
				&format!("Could not create VMA allocator!: {error:?}"),
			);
			std::ptr::null_mut()
		}
	}
}

/// # Safety
///
/// `allocator` must be null or come from `rx_gpu_allocator_create`, with every allocation
/// destroyed, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_allocator_free(allocator: *mut RxGpuAllocator)
{
	if !allocator.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(allocator) });
	}
}

/// # Safety
///
/// `buffer` and `allocation` must come from `rx_gpu_buffer_create_typed` on this allocator and not
/// be in use. `allocation` must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_buffer_destroy(
	allocator: *const RxGpuAllocator,
	buffer: u64,
	allocation: *mut RxGpuAllocation,
)
{
	if allocation.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe {
		let allocation = Box::from_raw(allocation);
		(*allocator)
			.0
			.destroy_buffer(vk::Buffer::from_raw(buffer), allocation.0);
	}
}

/// # Safety
///
/// `allocation` must be live and host visible. `out_mapped` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_allocation_map(
	allocator: *const RxGpuAllocator,
	allocation: *mut RxGpuAllocation,
	out_mapped: *mut *mut c_void,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { (*allocator).0.map(&mut (*allocation).0) } {
		Ok(mapped) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_mapped = mapped.cast() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `allocation` must be live and mapped by `rx_gpu_allocation_map`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_allocation_unmap(
	allocator: *const RxGpuAllocator,
	allocation: *mut RxGpuAllocation,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*allocator).0.unmap(&mut (*allocation).0) };
}

/// # Safety
///
/// `allocation` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_allocation_flush(
	allocator: *const RxGpuAllocator,
	allocation: *const RxGpuAllocation,
	offset: u64,
	size: u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	code(unsafe { (*allocator).0.flush(&(*allocation).0, offset, size) })
}

/// # Safety
///
/// `allocation` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_allocation_invalidate(
	allocator: *const RxGpuAllocator,
	allocation: *const RxGpuAllocation,
	offset: u64,
	size: u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	code(unsafe { (*allocator).0.invalidate(&(*allocation).0, offset, size) })
}

/// # Safety
///
/// `device` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_fence_create(
	device: *const RxGpuDevice,
	signaled: u8,
	out: *mut u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { &(*device).device }.create_fence(signaled != 0) {
		Ok(fence) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = fence.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `fence` a fence of it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_fence_wait(
	device: *const RxGpuDevice,
	fence: u64,
	timeout: u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	match device.wait_fence(vk::Fence::from_raw(fence), timeout) {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `fence` a fence of it that is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_fence_reset(device: *const RxGpuDevice, fence: u64) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	code(device.reset_fence(vk::Fence::from_raw(fence)))
}

/// # Safety
///
/// `device` must be live and `fence` a fence of it that is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_fence_destroy(device: *const RxGpuDevice, fence: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*device).device.destroy_fence(vk::Fence::from_raw(fence)) };
}

/// # Safety
///
/// `device` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_semaphore_create(
	device: *const RxGpuDevice,
	timeline: u8,
	out: *mut u64,
) -> i32
{
	let kind = if timeline != 0 {
		SemaphoreKind::Timeline
	} else {
		SemaphoreKind::Binary
	};

	// SAFETY: guaranteed by the caller.
	match unsafe { &(*device).device }.create_semaphore(kind) {
		Ok(semaphore) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = semaphore.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `semaphore` a semaphore of it that is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_semaphore_destroy(device: *const RxGpuDevice, semaphore: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_semaphore(vk::Semaphore::from_raw(semaphore))
	};
}

/// # Safety
///
/// `device` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_command_pool_create(
	device: *const RxGpuDevice,
	queue_family: u32,
	out: *mut u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { &(*device).device }.create_command_pool(queue_family) {
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
/// `device` must be live and `pool` a pool of it with no pending buffers.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_command_pool_reset(device: *const RxGpuDevice, pool: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &(*device).device }.reset_command_pool(vk::CommandPool::from_raw(pool));
}

/// # Safety
///
/// `device` must be live and `pool` a pool of it with no pending buffers.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_command_pool_destroy(device: *const RxGpuDevice, pool: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_command_pool(vk::CommandPool::from_raw(pool))
	};
}

/// # Safety
///
/// `device` must be live, `pool` a pool of it, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_command_buffer_allocate(
	device: *const RxGpuDevice,
	pool: u64,
	out: *mut *mut c_void,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	match device.allocate_command_buffer(vk::CommandPool::from_raw(pool)) {
		Ok(buffer) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = buffer.as_raw() as *mut c_void };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `buffer` a buffer allocated from `pool` that is not pending.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_command_buffer_free(
	device: *const RxGpuDevice,
	pool: u64,
	buffer: *mut c_void,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.free_command_buffer(
			vk::CommandPool::from_raw(pool),
			vk::CommandBuffer::from_raw(buffer as u64),
		)
	};
}

/// # Safety
///
/// `device` must be live and `buffer` one of its command buffers.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_command_buffer_begin(
	device: *const RxGpuDevice,
	buffer: *mut c_void,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	code(device.begin_command_buffer(vk::CommandBuffer::from_raw(buffer as u64)))
}

/// # Safety
///
/// `device` must be live and `buffer` one of its command buffers.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_command_buffer_end(
	device: *const RxGpuDevice,
	buffer: *mut c_void,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	code(device.end_command_buffer(vk::CommandBuffer::from_raw(buffer as u64)))
}

/// # Safety
///
/// `device` must be live and `buffer` one of its command buffers that is not pending.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_command_buffer_reset(
	device: *const RxGpuDevice,
	buffer: *mut c_void,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &(*device).device }.reset_command_buffer(vk::CommandBuffer::from_raw(buffer as u64));
}
