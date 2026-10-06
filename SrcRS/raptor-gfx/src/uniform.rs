use std::sync::Arc;

use ash::vk;
use raptor_gpu::{BufferType, Memory};

use crate::buffer::{FLAG_PERSISTENT_MAPPED, GpuBuffer};
use crate::core::GpuCore;

pub struct Uniforms {
	buffer: GpuBuffer,
	slot_index: u32,
	slot_size: u32,
	capacity: u32,
	page_size: u32,
	uniform_index: u32,
	frames: u32,
}

impl Uniforms {
	pub fn create(
		core: &Arc<GpuCore>,
		frames: u32,
		slot_size: u32,
		count: u32,
		buffer_type: BufferType,
	) -> Result<Self, vk::Result> {
		let page_size = slot_size * count;

		let buffer = GpuBuffer::with_data(
			core,
			buffer_type,
			u64::from(page_size) * u64::from(frames),
			Memory::AutoPreferDevice,
			FLAG_PERSISTENT_MAPPED,
		)?;

		Ok(Self {
			buffer,
			slot_index: 0,
			slot_size,
			capacity: count,
			page_size,
			uniform_index: 0,
			frames,
		})
	}

	pub fn gpu_buffer(&self) -> &GpuBuffer {
		&self.buffer
	}

	pub fn slot_index(&self) -> u32 {
		self.slot_index
	}

	pub fn slot_size(&self) -> u32 {
		self.slot_size
	}

	pub fn capacity(&self) -> u32 {
		self.capacity
	}

	pub fn page_size(&self) -> u32 {
		self.page_size
	}

	pub fn uniform_index(&self) -> u32 {
		self.uniform_index
	}

	pub fn base_offset(&self, frame: u32) -> u32 {
		self.page_size * frame
	}

	pub fn slot_offset(&self) -> u32 {
		self.slot_index * self.slot_size
	}

	fn page_mut(&mut self, frame: u32) -> &mut [u8] {
		let start = self.base_offset(frame) as usize;
		let page = self.page_size as usize;

		assert!(self.buffer.is_mapped(), "the uniform buffer is not mapped");

		// SAFETY: the buffer is persistently mapped and spans one page for each frame in flight,
		// and `&mut self` makes the borrow exclusive.
		unsafe { std::slice::from_raw_parts_mut(self.buffer.mapped_ptr().add(start), page) }
	}

	pub fn write_bytes(&mut self, frame: u32, bytes: &[u8]) -> bool {
		let size = bytes.len() as u32;

		if self.uniform_index + size >= self.page_size {
			raptor_core::log_error!(Render; "Could not submit uniform as buffer is full!");
			return false;
		}

		let start = (self.slot_offset() + self.uniform_index) as usize;
		let end = start + bytes.len();

		if end > self.page_size as usize {
			raptor_core::log_error!(Render; "Could not submit uniform as buffer is full!");
			return false;
		}

		self.page_mut(frame)[start..end].copy_from_slice(bytes);
		self.uniform_index += size;

		true
	}

	pub fn write<T: bytemuck::Pod>(&mut self, frame: u32, value: &T) -> bool {
		self.write_bytes(frame, bytemuck::bytes_of(value))
	}

	pub fn copy_from(&mut self, frame: u32, bytes: &[u8]) -> bool {
		if self.slot_offset() as usize + bytes.len() > self.page_size as usize {
			raptor_core::log_error!(Render; "Could not write buffer that is larger than uniform!");
			return false;
		}

		let start = self.slot_offset() as usize;

		self.page_mut(frame)[start..start + bytes.len()].copy_from_slice(bytes);

		true
	}

	pub fn copy_slots(&mut self, frame: u32, bytes: &[u8], count: u32) -> Option<u32> {
		if self.slot_index + count > self.capacity
			|| bytes.len() < (count * self.slot_size) as usize
		{
			return None;
		}

		let first_slot = self.slot_index;
		let start = self.slot_offset() as usize;
		let length = (count * self.slot_size) as usize;

		self.page_mut(frame)[start..start + length].copy_from_slice(&bytes[..length]);

		self.slot_index += count;
		self.uniform_index = 0;

		Some(first_slot)
	}

	pub fn next_slot(&mut self) {
		self.slot_index += 1;
		self.uniform_index = 0;
	}

	pub fn rewind(&mut self) {
		self.uniform_index = 0;
		self.slot_index = 0;
	}

	pub fn set_all_values(&mut self, value: &[u8], all_frames: bool) {
		let value_size = value.len();

		assert!(value_size > 0 && (self.page_size as usize).is_multiple_of(value_size));

		let page: Vec<u8> = value
			.iter()
			.copied()
			.cycle()
			.take(self.page_size as usize)
			.collect();

		let frames = if all_frames { self.frames } else { 1 };

		for frame in 0..frames {
			self.page_mut(frame).copy_from_slice(&page);
		}
	}

	pub fn flush_to_gpu(&self, frame: u32) {
		let _ = self.buffer.flush(
			u64::from(self.base_offset(frame)),
			u64::from(self.uniform_index) + 1,
		);
	}

	pub fn destroy(&self) {
		self.buffer.destroy();
	}
}

pub fn uniform_type() -> BufferType {
	BufferType::UniformWithOffset
}
