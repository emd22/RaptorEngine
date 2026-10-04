use ash::prelude::VkResult;
use ash::vk;

use crate::Device;

impl Device
{
	pub fn create_command_pool(&self, queue_family: u32) -> VkResult<vk::CommandPool>
	{
		let info = vk::CommandPoolCreateInfo::default()
			.flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER)
			.queue_family_index(queue_family);

		// SAFETY: the device is alive.
		unsafe { self.raw().create_command_pool(&info, None) }
	}

	pub fn reset_command_pool(&self, pool: vk::CommandPool)
	{
		// SAFETY: the pool belongs to this device and none of its buffers are pending.
		let _ = unsafe {
			self.raw()
				.reset_command_pool(pool, vk::CommandPoolResetFlags::empty())
		};
	}

	/// # Safety
	///
	/// The pool must belong to this device and none of its buffers may be pending.
	pub unsafe fn destroy_command_pool(&self, pool: vk::CommandPool)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_command_pool(pool, None) };
	}

	pub fn allocate_command_buffer(&self, pool: vk::CommandPool) -> VkResult<vk::CommandBuffer>
	{
		let info = vk::CommandBufferAllocateInfo::default()
			.command_pool(pool)
			.level(vk::CommandBufferLevel::PRIMARY)
			.command_buffer_count(1);

		// SAFETY: the pool belongs to this device.
		unsafe { self.raw().allocate_command_buffers(&info) }.map(|buffers| buffers[0])
	}

	/// # Safety
	///
	/// The buffer must come from `allocate_command_buffer` on this pool and not be pending.
	pub unsafe fn free_command_buffer(&self, pool: vk::CommandPool, buffer: vk::CommandBuffer)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().free_command_buffers(pool, &[buffer]) };
	}

	pub fn begin_command_buffer(&self, buffer: vk::CommandBuffer) -> VkResult<()>
	{
		// SAFETY: the buffer belongs to this device and is not being recorded elsewhere.
		unsafe {
			self.raw()
				.begin_command_buffer(buffer, &vk::CommandBufferBeginInfo::default())
		}
	}

	pub fn end_command_buffer(&self, buffer: vk::CommandBuffer) -> VkResult<()>
	{
		// SAFETY: the buffer is in the recording state.
		unsafe { self.raw().end_command_buffer(buffer) }
	}

	pub fn reset_command_buffer(&self, buffer: vk::CommandBuffer)
	{
		// SAFETY: the buffer is not pending and its pool allows individual resets.
		let _ = unsafe {
			self.raw()
				.reset_command_buffer(buffer, vk::CommandBufferResetFlags::empty())
		};
	}
}
