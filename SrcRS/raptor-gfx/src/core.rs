use std::sync::atomic::{AtomicU32, Ordering};

use raptor_gpu::{Allocator, BufferResource, Device, ImageResource, Instance, Level};

use crate::deletion::DeletionQueue;
use crate::limits::DELETION_FRAME_SPACING;

pub struct GpuCore {
	deletion: DeletionQueue,
	elapsed: AtomicU32,
	allocator: Allocator,
	device: Device,
	instance: Instance,
}

// SAFETY: the core owns Vulkan objects that are externally synchronised by their users, and the
// deletion queue is behind a mutex.
unsafe impl Send for GpuCore {}
// SAFETY: as above.
unsafe impl Sync for GpuCore {}

impl GpuCore {
	pub fn new(instance: Instance, device: Device, allocator: Allocator) -> Self {
		Self {
			deletion: DeletionQueue::new(),
			elapsed: AtomicU32::new(0),
			allocator,
			device,
			instance,
		}
	}

	pub fn device(&self) -> &Device {
		&self.device
	}

	pub fn allocator(&self) -> &Allocator {
		&self.allocator
	}

	pub fn instance(&self) -> &Instance {
		&self.instance
	}

	pub fn elapsed(&self) -> u32 {
		self.elapsed.load(Ordering::Relaxed)
	}

	pub fn set_elapsed(&self, elapsed: u32) {
		self.elapsed.store(elapsed, Ordering::Relaxed);
	}

	pub fn wait_idle(&self) {
		self.device.wait_idle();
	}

	pub fn retire(&self, task: impl FnOnce(&Device, &Allocator) + 'static) {
		self.deletion
			.push(self.elapsed() + DELETION_FRAME_SPACING, task);
	}

	pub fn retire_buffer(&self, resource: BufferResource) {
		self.retire(move |_, allocator| {
			// SAFETY: the deletion queue runs this only once the frames that could use the buffer
			// have finished, with the allocator the buffer was created with.
			unsafe { resource.destroy(allocator) };
		});
	}

	pub fn retire_image(&self, resource: ImageResource) {
		self.retire(move |device, allocator| {
			// SAFETY: the deletion queue runs this only once the image is unused, with the device
			// and allocator it was created with.
			unsafe { resource.destroy(device, allocator) };
		});
	}

	pub fn process_deletions(&self) -> usize {
		self.deletion
			.process(self.elapsed(), &self.device, &self.allocator)
	}

	pub fn flush_deletions(&self) -> usize {
		self.deletion.flush(&self.device, &self.allocator)
	}

	pub fn pending_deletions(&self) -> usize {
		self.deletion.len()
	}
}

impl Drop for GpuCore {
	fn drop(&mut self) {
		self.device.wait_idle();

		let freed = self.deletion.flush(&self.device, &self.allocator);

		if freed > 0 {
			self.device.log().log(
				Level::Debug,
				&format!("Freed {freed} queued GPU resources at shutdown"),
			);
		}
	}
}
