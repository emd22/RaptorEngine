use std::ffi::c_void;

use ash::vk;
use raptor_gpu::{BufferRecord, BufferResource, BufferType};

use crate::gpu_resources::RxGpuAllocator;
use crate::gpu_resources_typed::memory;

pub type RxBuffer = BufferRecord;
pub type RxBufferResource = BufferResource;

fn status_of(result: Result<(), vk::Result>) -> i32
{
	match result {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// Makes an empty buffer slot.
#[unsafe(no_mangle)]
pub extern "C" fn rx_buffer_new() -> *mut RxBuffer
{
	BufferRecord::into_raw(BufferRecord::new())
}

/// # Safety
///
/// `buffer` must come from `rx_buffer_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_retain(buffer: *mut RxBuffer)
{
	// SAFETY: guaranteed by the caller.
	unsafe { BufferRecord::retain(buffer) };
}

/// # Safety
///
/// `buffer` must come from `rx_buffer_new` and not be used by this reference afterwards.
/// `allocator` may be null to leak a buffer that is still in the slot, which must not be in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_release(buffer: *mut RxBuffer, allocator: *const RxGpuAllocator)
{
	// SAFETY: guaranteed by the caller.
	unsafe { BufferRecord::release(buffer, allocator.as_ref().map(|allocator| &allocator.0)) };
}

/// Makes a buffer in an empty slot.
///
/// # Safety
///
/// `buffer` and `allocator` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_create(
	buffer: *mut RxBuffer,
	allocator: *const RxGpuAllocator,
	buffer_type: u32,
	size: u64,
	memory_usage: u32,
	flags: u16,
) -> i32
{
	let Some(buffer_type) = BufferType::from_raw(buffer_type) else {
		return vk::Result::ERROR_INITIALIZATION_FAILED.as_raw();
	};

	// SAFETY: guaranteed by the caller.
	status_of(unsafe {
		(*buffer).create(
			&(*allocator).0,
			buffer_type,
			size,
			memory(memory_usage),
			flags,
		)
	})
}

/// Empties the slot. The result is null if it was empty, and otherwise must be given to
/// `rx_buffer_resource_destroy` once the GPU is done with the buffer.
///
/// # Safety
///
/// `buffer` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_detach(buffer: *mut RxBuffer) -> *mut RxBufferResource
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*buffer }
		.detach()
		.map_or(std::ptr::null_mut(), |resource| {
			Box::into_raw(Box::new(resource))
		})
}

/// # Safety
///
/// `resource` must be null or come from `rx_buffer_detach` on this allocator, not be in use by
/// pending work, and not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_resource_destroy(
	resource: *mut RxBufferResource,
	allocator: *const RxGpuAllocator,
)
{
	if resource.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	unsafe { Box::from_raw(resource).destroy(&(*allocator).0) };
}

/// # Safety
///
/// `buffer` must be live with host visible memory that is not mapped, and `allocator` the one it
/// was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_map(
	buffer: *mut RxBuffer,
	allocator: *const RxGpuAllocator,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	status_of(unsafe { (*buffer).map(&(*allocator).0) })
}

/// # Safety
///
/// `buffer` must be live, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_unmap(buffer: *mut RxBuffer, allocator: *const RxGpuAllocator)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (*buffer).unmap(&(*allocator).0) };
}

/// # Safety
///
/// `buffer` must be live with host visible memory at least `size` bytes large, `data` valid for
/// `size` bytes, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_upload(
	buffer: *mut RxBuffer,
	allocator: *const RxGpuAllocator,
	data: *const c_void,
	size: u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (buffer, allocator, data) = unsafe {
		(
			&*buffer,
			&(*allocator).0,
			std::slice::from_raw_parts(data.cast::<u8>(), size as usize),
		)
	};

	// SAFETY: guaranteed by the caller.
	status_of(unsafe { buffer.upload(allocator, data) })
}

/// # Safety
///
/// `buffer` must be live, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_flush(
	buffer: *const RxBuffer,
	allocator: *const RxGpuAllocator,
	offset: u64,
	size: u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	status_of(unsafe { (*buffer).flush(&(*allocator).0, offset, size) })
}

/// # Safety
///
/// `buffer` must be live, and `allocator` the one it was created with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_buffer_invalidate(
	buffer: *const RxBuffer,
	allocator: *const RxGpuAllocator,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	status_of(unsafe { (*buffer).invalidate(&(*allocator).0) })
}
