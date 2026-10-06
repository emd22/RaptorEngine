use std::ffi::CString;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

use ash::vk::{self, Handle};
use raptor_gpu::{QueueKind, SemaphoreKind, SubmitSignal};

use crate::core::GpuCore;

pub struct UploadContext {
	core: Arc<GpuCore>,
	family: u32,
	pool: vk::CommandPool,
	cmd: vk::CommandBuffer,
	fence: vk::Fence,
	immediate_pool: vk::CommandPool,
	immediate_cmd: vk::CommandBuffer,
	immediate_fence: vk::Fence,
	immediate_lock: Mutex<()>,
	transfer_semaphore: vk::Semaphore,
	transfer_count: AtomicU64,
}

impl UploadContext {
	pub fn new(core: &Arc<GpuCore>, family: u32) -> Result<Self, vk::Result> {
		let device = core.device();

		let pool = device.create_command_pool(family)?;
		let cmd = device.allocate_command_buffer(pool)?;
		let immediate_pool = device.create_command_pool(family)?;
		let immediate_cmd = device.allocate_command_buffer(immediate_pool)?;

		for (handle, label) in [(cmd, c"Upload"), (immediate_cmd, c"UploadImmediate")] {
			device.set_object_name(vk::ObjectType::COMMAND_BUFFER, handle.as_raw(), label);
		}

		Ok(Self {
			core: core.clone(),
			family,
			pool,
			cmd,
			fence: device.create_fence(true)?,
			immediate_pool,
			immediate_cmd,
			immediate_fence: device.create_fence(true)?,
			immediate_lock: Mutex::new(()),
			transfer_semaphore: device.create_semaphore(SemaphoreKind::Timeline)?,
			transfer_count: AtomicU64::new(0),
		})
	}

	pub fn core(&self) -> &Arc<GpuCore> {
		&self.core
	}

	pub fn family(&self) -> u32 {
		self.family
	}

	pub fn cmd(&self) -> vk::CommandBuffer {
		self.cmd
	}

	pub fn immediate_cmd(&self) -> vk::CommandBuffer {
		self.immediate_cmd
	}

	pub fn transfer_semaphore(&self) -> vk::Semaphore {
		self.transfer_semaphore
	}

	pub fn transfer_count(&self) -> u64 {
		self.transfer_count.load(Ordering::SeqCst)
	}

	pub fn begin_upload(&self) -> Result<(), vk::Result> {
		let device = self.core.device();

		device.wait_fence(self.fence, u64::MAX)?;
		device.reset_fence(self.fence)?;
		device.begin_command_buffer(self.cmd)
	}

	pub fn end_upload(&self) -> Result<(), vk::Result> {
		let device = self.core.device();

		device.end_command_buffer(self.cmd)?;

		let value = self.transfer_count() + 1;

		// SAFETY: the command buffer is recorded, and the fence unsignaled.
		unsafe {
			device.queue_submit(
				QueueKind::Transfer,
				&[],
				&[self.cmd],
				&[SubmitSignal {
					semaphore: self.transfer_semaphore,
					value,
				}],
				self.fence,
			)
		}?;

		self.transfer_count.store(value, Ordering::SeqCst);

		device.wait_fence(self.fence, u64::MAX)
	}

	pub fn wait_for_uploads(&self) -> Result<(), vk::Result> {
		self.core.device().wait_fence(self.fence, u64::MAX)
	}

	pub fn immediate(&self, record: impl FnOnce(vk::CommandBuffer)) -> Result<(), vk::Result> {
		let _guard = self
			.immediate_lock
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());
		let device = self.core.device();

		device.begin_command_buffer(self.immediate_cmd)?;
		record(self.immediate_cmd);
		device.end_command_buffer(self.immediate_cmd)?;

		device.reset_fence(self.immediate_fence)?;

		// SAFETY: the command buffer is recorded, and the fence unsignaled.
		unsafe {
			device.queue_submit(
				QueueKind::Transfer,
				&[],
				&[self.immediate_cmd],
				&[],
				self.immediate_fence,
			)
		}?;

		device.wait_fence(self.immediate_fence, u64::MAX)?;
		device.reset_fence(self.immediate_fence)?;
		device.reset_command_buffer(self.immediate_cmd);

		Ok(())
	}

	pub fn name_label(&self, label: &str) {
		if let Ok(label) = CString::new(label) {
			self.core.device().set_object_name(
				vk::ObjectType::COMMAND_BUFFER,
				self.cmd.as_raw(),
				&label,
			);
		}
	}
}

impl Drop for UploadContext {
	fn drop(&mut self) {
		let device = self.core.device();

		device.wait_idle();

		// SAFETY: nothing is pending once the device is idle, and the handles are this
		// context's own.
		unsafe {
			device.free_command_buffer(self.pool, self.cmd);
			device.free_command_buffer(self.immediate_pool, self.immediate_cmd);
			device.destroy_command_pool(self.pool);
			device.destroy_command_pool(self.immediate_pool);
			device.destroy_fence(self.fence);
			device.destroy_fence(self.immediate_fence);
			device.destroy_semaphore(self.transfer_semaphore);
		}
	}
}
