use std::ffi::c_void;

use ash::vk;
use raptor_gpu::{BufferRecord, BufferType};

use crate::gpu_resources::RxGpuAllocator;
use crate::gpu_resources_typed::memory;

pub type RxBuffer = BufferRecord;

/// # Safety
///
/// `allocator` must be live and `out_status` writable. The result is null on failure, and otherwise
/// must be given to `rx_buffer_destroy` with the same allocator.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_create(
	allocator: *const RxGpuAllocator,
	buffer_type: u32,
	size: u64,
	memory_usage: u32,
	flags: u16,
	out_status: *mut i32,
) -> *mut RxBuffer
{
	let Some(buffer_type) = BufferType::from_raw(buffer_type) else {
		// SAFETY: guaranteed by the caller.
		unsafe { *out_status = vk::Result::ERROR_INITIALIZATION_FAILED.as_raw() };
		return std::ptr::null_mut();
	};

	// SAFETY: guaranteed by the caller.
	let allocator = unsafe { &(*allocator).0 };

	// SAFETY: the allocator is live.
	let created =
		unsafe { BufferRecord::create(allocator, buffer_type, size, memory(memory_usage), flags) };

	match created {
		Ok(record) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_status = vk::Result::SUCCESS.as_raw() };
			Box::into_raw(record)
		}
		Err(error) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_status = error.as_raw() };
			std::ptr::null_mut()
		}
	}
}

/// # Safety
///
/// `record` must come from `rx_buffer_create` on this allocator, not be in use by pending work, and
/// not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_destroy(record: *mut RxBuffer, allocator: *const RxGpuAllocator)
{
	if record.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe { Box::from_raw(record).destroy(&(*allocator).0) };
}

/// # Safety
///
/// `record` must be live with host visible memory, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_map(
	record: *mut RxBuffer,
	allocator: *const RxGpuAllocator,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { (*record).map(&(*allocator).0) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `record` must be live, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_unmap(record: *mut RxBuffer, allocator: *const RxGpuAllocator)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*record).unmap(&(*allocator).0) };
}

/// # Safety
///
/// `record` must be live with host visible memory at least `size` bytes large, `data` valid for
/// `size` bytes, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_upload(
	record: *mut RxBuffer,
	allocator: *const RxGpuAllocator,
	data: *const c_void,
	size: u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (record, allocator, data) = unsafe {
		(
			&mut *record,
			&(*allocator).0,
			std::slice::from_raw_parts(data.cast::<u8>(), size as usize),
		)
	};

	// SAFETY: guaranteed by the caller.
	match unsafe { record.upload(allocator, data) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `record` must be live, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_flush(
	record: *const RxBuffer,
	allocator: *const RxGpuAllocator,
	offset: u64,
	size: u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { (*record).flush(&(*allocator).0, offset, size) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `record` must be live, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_invalidate(
	record: *const RxBuffer,
	allocator: *const RxGpuAllocator,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { (*record).invalidate(&(*allocator).0) } {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}
