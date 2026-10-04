use ash::prelude::VkResult;
use ash::vk;

use crate::Device;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum QueueKind
{
	Graphics = 0,
	Present = 1,
	Transfer = 2,
}

impl QueueKind
{
	pub fn from_raw(raw: u32) -> Option<Self>
	{
		Some(match raw {
			0 => Self::Graphics,
			1 => Self::Present,
			2 => Self::Transfer,
			_ => return None,
		})
	}
}

#[derive(Clone, Copy, Debug)]
pub struct SubmitWait
{
	pub semaphore: vk::Semaphore,
	pub stages: vk::PipelineStageFlags,
	pub value: u64,
}

#[derive(Clone, Copy, Debug)]
pub struct SubmitSignal
{
	pub semaphore: vk::Semaphore,
	pub value: u64,
}

impl Device
{
	/// Submits to a queue while holding the queue lock. `value` of a binary semaphore is ignored.
	///
	/// # Safety
	///
	/// Every handle must belong to this device, the command buffers must be recorded and not
	/// pending, and `fence` must be null or unsignaled.
	pub unsafe fn queue_submit(
		&self,
		kind: QueueKind,
		waits: &[SubmitWait],
		commands: &[vk::CommandBuffer],
		signals: &[SubmitSignal],
		fence: vk::Fence,
	) -> VkResult<()>
	{
		let wait_semaphores: Vec<_> = waits.iter().map(|wait| wait.semaphore).collect();
		let wait_stages: Vec<_> = waits.iter().map(|wait| wait.stages).collect();
		let wait_values: Vec<_> = waits.iter().map(|wait| wait.value).collect();
		let signal_semaphores: Vec<_> = signals.iter().map(|signal| signal.semaphore).collect();
		let signal_values: Vec<_> = signals.iter().map(|signal| signal.value).collect();

		let mut timeline = vk::TimelineSemaphoreSubmitInfo::default()
			.wait_semaphore_values(&wait_values)
			.signal_semaphore_values(&signal_values);

		let mut info = vk::SubmitInfo::default()
			.wait_semaphores(&wait_semaphores)
			.wait_dst_stage_mask(&wait_stages)
			.command_buffers(commands)
			.signal_semaphores(&signal_semaphores);

		if !waits.is_empty() || !signals.is_empty() {
			info = info.push_next(&mut timeline);
		}

		let _queues = self.lock_queues();

		// SAFETY: guaranteed by the caller, and the queue lock is held.
		unsafe { self.raw().queue_submit(self.queue(kind), &[info], fence) }
	}

	pub fn queue_wait_idle(&self, kind: QueueKind) -> VkResult<()>
	{
		let _queues = self.lock_queues();

		// SAFETY: the queue belongs to this device and the queue lock is held.
		unsafe { self.raw().queue_wait_idle(self.queue(kind)) }
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn queue_kinds_round_trip_their_raw_values()
	{
		for kind in [QueueKind::Graphics, QueueKind::Present, QueueKind::Transfer] {
			assert_eq!(QueueKind::from_raw(kind as u32), Some(kind));
		}

		assert_eq!(QueueKind::from_raw(3), None);
	}
}
