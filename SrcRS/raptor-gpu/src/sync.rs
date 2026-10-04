use ash::prelude::VkResult;
use ash::vk;

use crate::Device;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SemaphoreKind
{
	Binary,
	Timeline,
}

impl Device
{
	pub fn create_fence(&self, signaled: bool) -> VkResult<vk::Fence>
	{
		let flags = if signaled {
			vk::FenceCreateFlags::SIGNALED
		} else {
			vk::FenceCreateFlags::empty()
		};

		// SAFETY: the device is alive.
		unsafe {
			self.raw()
				.create_fence(&vk::FenceCreateInfo::default().flags(flags), None)
		}
	}

	pub fn wait_fence(&self, fence: vk::Fence, timeout: u64) -> VkResult<()>
	{
		// SAFETY: the fence belongs to this device.
		unsafe { self.raw().wait_for_fences(&[fence], true, timeout) }
	}

	pub fn reset_fence(&self, fence: vk::Fence) -> VkResult<()>
	{
		// SAFETY: the fence belongs to this device and is not in use by a pending submission.
		unsafe { self.raw().reset_fences(&[fence]) }
	}

	/// # Safety
	///
	/// The fence must belong to this device and not be in use.
	pub unsafe fn destroy_fence(&self, fence: vk::Fence)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_fence(fence, None) };
	}

	pub fn create_semaphore(&self, kind: SemaphoreKind) -> VkResult<vk::Semaphore>
	{
		let semaphore_type = match kind {
			SemaphoreKind::Binary => vk::SemaphoreType::BINARY,
			SemaphoreKind::Timeline => vk::SemaphoreType::TIMELINE,
		};

		let mut type_info = vk::SemaphoreTypeCreateInfo::default()
			.semaphore_type(semaphore_type)
			.initial_value(0);

		// SAFETY: the device is alive.
		unsafe {
			self.raw().create_semaphore(
				&vk::SemaphoreCreateInfo::default().push_next(&mut type_info),
				None,
			)
		}
	}

	/// # Safety
	///
	/// The semaphore must belong to this device and not be in use.
	pub unsafe fn destroy_semaphore(&self, semaphore: vk::Semaphore)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_semaphore(semaphore, None) };
	}
}
