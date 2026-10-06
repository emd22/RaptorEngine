use std::sync::Arc;

use raptor_gfx::limits::FRAMES_IN_FLIGHT;
use raptor_gfx::{GpuBuffer, GpuCore};
use raptor_gpu::{BufferType, Memory};

pub const MAX_OBJECTS: u32 = raptor_scene::scene::MAX_OBJECTS;
pub const ENTRY_SIZE: u32 = 64;
pub const PAGE_SIZE: u32 = MAX_OBJECTS * ENTRY_SIZE;

/// Where the model matrices of the objects are put for the shaders: one page for each frame in
/// flight, each with a matrix for every object slot.
pub struct ObjectBuffer
{
	buffer: GpuBuffer,
}

impl ObjectBuffer
{
	pub fn new(core: &Arc<GpuCore>) -> Result<Self, ash::vk::Result>
	{
		let buffer = GpuBuffer::with_data(
			core,
			BufferType::StorageWithOffset,
			u64::from(PAGE_SIZE) * u64::from(FRAMES_IN_FLIGHT),
			Memory::CpuOnly,
			raptor_gfx::buffer::FLAG_PERSISTENT_MAPPED,
		)?;

		Ok(Self { buffer })
	}

	pub fn buffer(&self) -> &GpuBuffer
	{
		&self.buffer
	}

	pub fn base_offset(frame: u32) -> u32
	{
		frame * PAGE_SIZE
	}

	pub fn submit(&self, frame: u32, object: u32, matrix: &[f32; 16])
	{
		assert!(object < MAX_OBJECTS, "the object has no slot in the object buffer");

		let mapped = self.buffer.mapped_ptr();

		if mapped.is_null() {
			return;
		}

		let offset = (Self::base_offset(frame) + object * ENTRY_SIZE) as usize;

		// SAFETY: the buffer is persistently mapped with a page of `MAX_OBJECTS` entries for each
		// frame in flight, and `offset` is within the page of `frame`.
		unsafe {
			std::ptr::copy_nonoverlapping(
				matrix.as_ptr().cast::<u8>(),
				mapped.add(offset),
				ENTRY_SIZE as usize,
			);
		}
	}
}
