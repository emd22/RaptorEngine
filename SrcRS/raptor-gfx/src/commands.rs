use std::cell::Cell;

use ash::vk;
use raptor_gpu::Device;

pub struct CommandBuffer {
	raw: vk::CommandBuffer,
	queue_family: u32,
	bound_pipeline: Cell<u64>,
}

// SAFETY: a command buffer is recorded from one thread at a time, like the Vulkan handle it
// wraps, and the bookkeeping cell is only touched while recording.
unsafe impl Send for CommandBuffer {}
// SAFETY: as above.
unsafe impl Sync for CommandBuffer {}

impl CommandBuffer {
	pub fn new(raw: vk::CommandBuffer, queue_family: u32) -> Self {
		Self {
			raw,
			queue_family,
			bound_pipeline: Cell::new(0),
		}
	}

	pub fn raw(&self) -> vk::CommandBuffer {
		self.raw
	}

	pub fn queue_family(&self) -> u32 {
		self.queue_family
	}

	pub fn bound_pipeline(&self) -> u64 {
		self.bound_pipeline.get()
	}

	pub fn set_bound_pipeline(&self, pipeline: u64) {
		self.bound_pipeline.set(pipeline);
	}

	pub fn begin(&self, device: &Device) -> Result<(), vk::Result> {
		self.bound_pipeline.set(0);

		device.begin_command_buffer(self.raw)
	}

	pub fn end(&self, device: &Device) -> Result<(), vk::Result> {
		device.end_command_buffer(self.raw)
	}

	pub fn reset(&self, device: &Device) {
		device.reset_command_buffer(self.raw);
	}
}
