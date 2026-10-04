use std::ffi::c_void;

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

#[repr(C)]
pub struct BufferRecord
{
	pub fields: BufferFields,
	allocation: Allocation,
}

impl BufferRecord
{
	/// # Safety
	///
	/// The allocator must be live.
	pub unsafe fn create(
		allocator: &Allocator,
		buffer_type: BufferType,
		size: u64,
		memory: Memory,
		flags: u16,
	) -> VkResult<Box<Self>>
	{
		// SAFETY: guaranteed by the caller.
		let buffer = unsafe { Buffer::create(allocator, buffer_type, size, memory, flags) }?;

		Ok(Box::new(Self {
			fields: BufferFields {
				buffer: buffer.handle.as_raw(),
				size,
				mapped: buffer.mapped.cast(),
				buffer_type: buffer_type as u32,
				flags,
			},
			allocation: buffer.allocation,
		}))
	}

	pub fn is_mapped(&self) -> bool
	{
		!self.fields.mapped.is_null()
	}

	/// # Safety
	///
	/// The allocator must be the one the buffer was created with, and the memory must be host
	/// visible.
	pub unsafe fn map(&mut self, allocator: &Allocator) -> VkResult<()>
	{
		// SAFETY: guaranteed by the caller.
		let mapped = unsafe { allocator.map(&mut self.allocation) }?;

		self.fields.mapped = mapped.cast();

		Ok(())
	}

	/// # Safety
	///
	/// The allocator must be the one the buffer was created with.
	pub unsafe fn unmap(&mut self, allocator: &Allocator)
	{
		if !self.is_mapped() {
			return;
		}

		// SAFETY: guaranteed by the caller, and the buffer is mapped.
		unsafe { allocator.unmap(&mut self.allocation) };

		self.fields.mapped = std::ptr::null_mut();
	}

	/// # Safety
	///
	/// As for `map`. `data` must be no larger than the buffer.
	pub unsafe fn upload(&mut self, allocator: &Allocator, data: &[u8]) -> VkResult<()>
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
				self.fields.mapped.cast::<u8>(),
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
		allocator.flush(&self.allocation, offset, size)
	}

	pub fn invalidate(&self, allocator: &Allocator) -> VkResult<()>
	{
		allocator.invalidate(&self.allocation, 0, self.fields.size)
	}

	/// # Safety
	///
	/// The buffer must not be in use by pending work, and the allocator must be the one it was
	/// created with.
	pub unsafe fn destroy(self, allocator: &Allocator)
	{
		// SAFETY: guaranteed by the caller.
		unsafe {
			allocator.destroy_buffer(vk::Buffer::from_raw(self.fields.buffer), self.allocation)
		};
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
}
