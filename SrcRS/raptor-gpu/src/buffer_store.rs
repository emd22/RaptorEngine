use std::cell::UnsafeCell;
use std::ffi::c_void;
use std::sync::Arc;

use ash::prelude::VkResult;
use ash::vk::{self, Handle};

use crate::{Allocation, Allocator, Buffer, BufferType, Memory};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct BufferFields
{
	pub buffer: u64,
	pub size: u64,
	pub mapped: *mut c_void,
	pub buffer_type: u32,
	pub flags: u16,
}

/// A buffer that is detached from its slot, to be destroyed once the GPU is done with it.
pub struct BufferResource
{
	buffer: vk::Buffer,
	allocation: Allocation,
}

impl BufferResource
{
	/// # Safety
	///
	/// The allocator must be the one the buffer was created with, and the buffer must not be in
	/// use.
	pub unsafe fn destroy(self, allocator: &Allocator)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { allocator.destroy_buffer(self.buffer, self.allocation) };
	}
}

/// The place a buffer lives. Whatever is made in it can be replaced or destroyed while things that
/// refer to the slot, such as descriptor sets, stay valid and see the current buffer.
///
/// The fields are read from C++ through the pointer, so access has to be externally synchronised,
/// like the Vulkan handles they hold.
#[repr(C)]
pub struct BufferRecord
{
	fields: UnsafeCell<BufferFields>,
	allocation: UnsafeCell<Option<Allocation>>,
}

impl BufferRecord
{
	#[allow(clippy::arc_with_non_send_sync)]
	pub fn new() -> Arc<Self>
	{
		Arc::new(Self {
			fields: UnsafeCell::new(BufferFields {
				buffer: 0,
				size: 0,
				mapped: std::ptr::null_mut(),
				buffer_type: BufferType::Storage as u32,
				flags: 0,
			}),
			allocation: UnsafeCell::new(None),
		})
	}

	pub fn into_raw(this: Arc<Self>) -> *mut Self
	{
		Arc::into_raw(this).cast_mut()
	}

	pub fn fields(&self) -> BufferFields
	{
		// SAFETY: access is externally synchronised, and no reference into the cell outlives a
		// call.
		unsafe { *self.fields.get() }
	}

	pub fn has_buffer(&self) -> bool
	{
		self.fields().buffer != 0
	}

	pub fn is_mapped(&self) -> bool
	{
		!self.fields().mapped.is_null()
	}

	/// # Safety
	///
	/// `record` must come from `into_raw`.
	pub unsafe fn retain(record: *const Self)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { Arc::increment_strong_count(record) };
	}

	/// Drops a reference. If it was the last and the slot still holds a buffer, the buffer is
	/// destroyed at once when there is an allocator, and leaked otherwise.
	///
	/// # Safety
	///
	/// `record` must come from `into_raw` and not be used by the caller afterwards, and a buffer
	/// left in the slot must not be in use.
	pub unsafe fn release(record: *const Self, allocator: Option<&Allocator>) -> bool
	{
		// SAFETY: guaranteed by the caller.
		let Some(record) = Arc::into_inner(unsafe { Arc::from_raw(record) }) else {
			return false;
		};

		if let Some(allocator) = allocator
			&& let Some(resource) = record.detach()
		{
			// SAFETY: guaranteed by the caller.
			unsafe { resource.destroy(allocator) };
		}

		true
	}

	/// Drops a reference held as an `Arc`, destroying a buffer left in the slot if it was the last
	/// one.
	///
	/// # Safety
	///
	/// As for `release`.
	pub unsafe fn release_arc(this: Arc<Self>, allocator: &Allocator) -> bool
	{
		let Some(record) = Arc::into_inner(this) else {
			return false;
		};

		if let Some(resource) = record.detach() {
			// SAFETY: guaranteed by the caller, and the last reference is gone.
			unsafe { resource.destroy(allocator) };
		}

		true
	}

	/// Makes a buffer in the slot, which must be empty.
	///
	/// # Safety
	///
	/// The allocator must be live, and nothing else may access the record during the call.
	pub unsafe fn create(
		&self,
		allocator: &Allocator,
		buffer_type: BufferType,
		size: u64,
		memory: Memory,
		flags: u16,
	) -> VkResult<()>
	{
		debug_assert!(!self.has_buffer(), "the slot already holds a buffer");

		// SAFETY: guaranteed by the caller.
		let buffer = unsafe { Buffer::create(allocator, buffer_type, size, memory, flags) }?;

		// SAFETY: guaranteed by the caller.
		unsafe {
			*self.fields.get() = BufferFields {
				buffer: buffer.handle.as_raw(),
				size,
				mapped: buffer.mapped.cast(),
				buffer_type: buffer_type as u32,
				flags,
			};
			*self.allocation.get() = Some(buffer.allocation);
		}

		Ok(())
	}

	/// Empties the slot, handing the buffer over to be destroyed later. The size and mapping are
	/// cleared, and the type is kept.
	pub fn detach(&self) -> Option<BufferResource>
	{
		// SAFETY: access is externally synchronised, and no reference into the cells outlives a
		// call.
		let (fields, allocation) =
			unsafe { (&mut *self.fields.get(), &mut *self.allocation.get()) };

		let allocation = allocation.take()?;
		let buffer = vk::Buffer::from_raw(fields.buffer);

		fields.buffer = 0;
		fields.size = 0;
		fields.mapped = std::ptr::null_mut();

		Some(BufferResource { buffer, allocation })
	}

	/// # Safety
	///
	/// The allocator must be the one the buffer was created with, the buffer must be host visible
	/// and not already mapped, and nothing else may access the record during the call.
	pub unsafe fn map(&self, allocator: &Allocator) -> VkResult<()>
	{
		// SAFETY: guaranteed by the caller.
		let (fields, allocation) =
			unsafe { (&mut *self.fields.get(), &mut *self.allocation.get()) };

		let Some(allocation) = allocation.as_mut() else {
			return Err(vk::Result::ERROR_MEMORY_MAP_FAILED);
		};

		// SAFETY: guaranteed by the caller.
		fields.mapped = unsafe { allocator.map(allocation) }?.cast();

		Ok(())
	}

	/// # Safety
	///
	/// As for `map`.
	pub unsafe fn unmap(&self, allocator: &Allocator)
	{
		// SAFETY: guaranteed by the caller.
		let (fields, allocation) =
			unsafe { (&mut *self.fields.get(), &mut *self.allocation.get()) };

		if fields.mapped.is_null() {
			return;
		}

		if let Some(allocation) = allocation.as_mut() {
			// SAFETY: guaranteed by the caller, and the buffer is mapped.
			unsafe { allocator.unmap(allocation) };
		}

		fields.mapped = std::ptr::null_mut();
	}

	/// Copies `data` in, mapping the buffer around it if it is not mapped already.
	///
	/// # Safety
	///
	/// As for `map`, and `data` must be no larger than the buffer.
	pub unsafe fn upload(&self, allocator: &Allocator, data: &[u8]) -> VkResult<()>
	{
		let was_mapped = self.is_mapped();

		if !was_mapped {
			// SAFETY: guaranteed by the caller.
			unsafe { self.map(allocator) }?;
		}

		// SAFETY: the buffer is mapped and at least as large as `data`.
		unsafe {
			std::ptr::copy_nonoverlapping(
				data.as_ptr(),
				self.fields().mapped.cast::<u8>(),
				data.len(),
			)
		};

		if !was_mapped {
			// SAFETY: it was mapped above.
			unsafe { self.unmap(allocator) };
		}

		Ok(())
	}

	pub fn flush(&self, allocator: &Allocator, offset: u64, size: u64) -> VkResult<()>
	{
		// SAFETY: access is externally synchronised, and the reference does not outlive the call.
		let allocation = unsafe { &*self.allocation.get() };

		match allocation {
			Some(allocation) => allocator.flush(allocation, offset, size),
			None => Ok(()),
		}
	}

	pub fn invalidate(&self, allocator: &Allocator) -> VkResult<()>
	{
		// SAFETY: access is externally synchronised, and the reference does not outlive the call.
		let allocation = unsafe { &*self.allocation.get() };

		match allocation {
			Some(allocation) => allocator.invalidate(allocation, 0, self.fields().size),
			None => Ok(()),
		}
	}
}

#[cfg(test)]
mod tests
{
	use std::mem::{offset_of, size_of};

	use super::*;

	#[test]
	fn the_public_fields_have_the_layout_the_c_header_declares()
	{
		assert_eq!(offset_of!(BufferRecord, fields), 0);
		assert_eq!(size_of::<BufferFields>(), 32);
		assert_eq!(offset_of!(BufferFields, buffer), 0);
		assert_eq!(offset_of!(BufferFields, size), 8);
		assert_eq!(offset_of!(BufferFields, mapped), 16);
		assert_eq!(offset_of!(BufferFields, buffer_type), 24);
		assert_eq!(offset_of!(BufferFields, flags), 28);
	}

	#[test]
	fn a_new_slot_is_empty_and_detaching_it_gives_nothing()
	{
		let slot = BufferRecord::new();

		assert!(!slot.has_buffer());
		assert!(!slot.is_mapped());
		assert_eq!(slot.fields().buffer_type, BufferType::Storage as u32);
		assert!(slot.detach().is_none());
	}

	#[test]
	fn the_last_release_is_reported_once()
	{
		let slot = BufferRecord::into_raw(BufferRecord::new());

		// SAFETY: the slot was just made and is only used through these calls.
		unsafe {
			BufferRecord::retain(slot);

			assert!(!BufferRecord::release(slot, None));
			assert!(BufferRecord::release(slot, None));
		}
	}
}
