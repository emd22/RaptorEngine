use std::ffi::c_void;
use std::sync::Arc;

use ash::vk::{self, Handle};
use raptor_gpu::{BufferFields, BufferRecord, BufferType, Memory};

use crate::core::GpuCore;

pub const FLAG_PERSISTENT_MAPPED: u16 = raptor_gpu::BUFFER_PERSISTENT_MAPPED;
pub const FLAG_TRANSFER_RECEIVER: u16 = raptor_gpu::BUFFER_TRANSFER_RECEIVER;

pub struct GpuBuffer {
	core: Arc<GpuCore>,
	record: Arc<BufferRecord>,
}

// SAFETY: like the Vulkan handles it holds, a buffer's slot is externally synchronised by its
// users: one thread creates, maps and destroys it at a time.
unsafe impl Send for GpuBuffer {}
// SAFETY: as above, shared references only read the fields or call Vulkan entry points that are
// safe to call concurrently.
unsafe impl Sync for GpuBuffer {}

impl GpuBuffer {
	pub fn new(core: &Arc<GpuCore>) -> Self {
		Self {
			core: core.clone(),
			record: BufferRecord::new(),
		}
	}

	pub fn with_data(
		core: &Arc<GpuCore>,
		buffer_type: BufferType,
		size: u64,
		memory: Memory,
		flags: u16,
	) -> Result<Self, vk::Result> {
		let buffer = Self::new(core);
		buffer.create(buffer_type, size, memory, flags)?;
		Ok(buffer)
	}

	pub fn create(
		&self,
		buffer_type: BufferType,
		size: u64,
		memory: Memory,
		flags: u16,
	) -> Result<(), vk::Result> {
		assert!(size > 0, "a GPU buffer needs a size");
		assert!(buffer_type != BufferType::None, "a GPU buffer needs a type");

		self.destroy();

		// SAFETY: the allocator is live for as long as the core is, and the slot was emptied.
		unsafe {
			self.record
				.create(self.core.allocator(), buffer_type, size, memory, flags)
		}
	}

	pub fn destroy(&self) {
		if let Some(resource) = self.record.detach() {
			self.core.retire_buffer(resource);
		}
	}

	pub fn core(&self) -> &Arc<GpuCore> {
		&self.core
	}

	pub fn record(&self) -> &Arc<BufferRecord> {
		&self.record
	}

	pub fn fields(&self) -> BufferFields {
		self.record.fields()
	}

	pub fn is_created(&self) -> bool {
		self.record.has_buffer()
	}

	pub fn handle(&self) -> vk::Buffer {
		vk::Buffer::from_raw(self.fields().buffer)
	}

	pub fn size(&self) -> u64 {
		self.fields().size
	}

	pub fn buffer_type(&self) -> BufferType {
		BufferType::from_raw(self.fields().buffer_type).unwrap_or(BufferType::None)
	}

	pub fn mapped_ptr(&self) -> *mut u8 {
		self.fields().mapped.cast::<c_void>().cast::<u8>()
	}

	pub fn is_mapped(&self) -> bool {
		self.record.is_mapped()
	}

	pub fn map(&self) -> Result<(), vk::Result> {
		if self.is_mapped() {
			return Ok(());
		}

		// SAFETY: the allocator is the buffer's own, and the buffer is not mapped.
		unsafe { self.record.map(self.core.allocator()) }
	}

	pub fn unmap(&self) {
		if self.is_mapped() {
			// SAFETY: the buffer is mapped and the allocator is the buffer's own.
			unsafe { self.record.unmap(self.core.allocator()) };
		}
	}

	pub fn upload(&self, data: &[u8]) -> Result<(), vk::Result> {
		assert!(!data.is_empty(), "nothing to upload");
		assert!(
			self.size() >= data.len() as u64,
			"GPU buffer is smaller than source buffer!"
		);

		// SAFETY: the allocator is the buffer's own, and the data fits the buffer.
		unsafe { self.record.upload(self.core.allocator(), data) }
	}

	pub fn upload_slice<T: bytemuck::Pod>(&self, data: &[T]) -> Result<(), vk::Result> {
		self.upload(bytemuck::cast_slice(data))
	}

	pub fn flush(&self, offset: u64, size: u64) -> Result<(), vk::Result> {
		self.record.flush(self.core.allocator(), offset, size)
	}

	pub fn invalidate(&self) -> Result<(), vk::Result> {
		self.record.invalidate(self.core.allocator())
	}

	pub fn with_mapped<R>(&self, f: impl FnOnce(&mut [u8]) -> R) -> Result<R, vk::Result> {
		let was_mapped = self.is_mapped();

		self.map()?;

		let size = self.size() as usize;

		// SAFETY: the buffer is mapped, spans `size` bytes, and the closure borrows it
		// exclusively for the call.
		let result = f(unsafe { std::slice::from_raw_parts_mut(self.mapped_ptr(), size) });

		if !was_mapped {
			self.unmap();
		}

		Ok(result)
	}
}

impl Drop for GpuBuffer {
	fn drop(&mut self) {
		self.destroy();
	}
}
