use std::ffi::CString;
use std::sync::atomic::{AtomicU32, Ordering};

use ash::prelude::VkResult;
use ash::vk::{self, Handle};

use crate::{Device, QueueKind, SemaphoreKind, SubmitSignal, SubmitWait};

#[repr(C)]
pub struct FrameLoopFields
{
	pub frame_number: AtomicU32,
	pub elapsed: AtomicU32,
	pub image_index: AtomicU32,
}

struct FrameSync
{
	image_available: vk::Semaphore,
	in_flight: vk::Fence,
}

#[repr(C)]
pub struct FrameLoop
{
	pub fields: FrameLoopFields,
	frames: Vec<FrameSync>,
	submit_semaphores: Vec<vk::Semaphore>,
}

fn name_object(device: &Device, object_type: vk::ObjectType, handle: u64, name: &str)
{
	if let Ok(name) = CString::new(name) {
		device.set_object_name(object_type, handle, &name);
	}
}

impl FrameLoop
{
	/// Makes the per frame synchronisation, and one submit semaphore for each swapchain image,
	/// since presenting waits on the image's own semaphore rather than the frame's.
	pub fn create(device: &Device, frames_in_flight: u32, image_count: u32) -> VkResult<Box<Self>>
	{
		let mut frame_loop = Box::new(Self {
			fields: FrameLoopFields {
				frame_number: AtomicU32::new(0),
				elapsed: AtomicU32::new(0),
				image_index: AtomicU32::new(0),
			},
			frames: Vec::with_capacity(frames_in_flight as usize),
			submit_semaphores: Vec::with_capacity(image_count as usize),
		});

		for index in 0..frames_in_flight {
			let in_flight = device.create_fence(true)?;
			let image_available = device.create_semaphore(SemaphoreKind::Binary)?;

			name_object(
				device,
				vk::ObjectType::SEMAPHORE,
				image_available.as_raw(),
				&format!("Frame {index} I.A."),
			);

			frame_loop.frames.push(FrameSync {
				image_available,
				in_flight,
			});
		}

		for _ in 0..image_count {
			let semaphore = device.create_semaphore(SemaphoreKind::Binary)?;
			frame_loop.submit_semaphores.push(semaphore);
		}

		Ok(frame_loop)
	}

	fn frame(&self) -> &FrameSync
	{
		&self.frames[self.fields.frame_number.load(Ordering::Relaxed) as usize]
	}

	/// Waits for the GPU to finish the frame that last used this slot, and readies its fence.
	pub fn begin_frame(&self, device: &Device) -> VkResult<()>
	{
		let fence = self.frame().in_flight;

		device.wait_fence(fence, u64::MAX)?;
		device.reset_fence(fence)
	}

	/// Gets the swapchain image to render to, which is left in `fields.image_index`.
	pub fn acquire(&self, device: &Device, swapchain: vk::SwapchainKHR) -> vk::Result
	{
		let (result, index) =
			device.acquire_next_image(swapchain, u64::MAX, self.frame().image_available);

		self.fields.image_index.store(index, Ordering::Relaxed);

		result
	}

	/// Submits the frame's commands, waiting for the acquired image and for the uploads up to
	/// `transfer_value`, and presents the image. Returns the present result.
	///
	/// # Safety
	///
	/// `commands` must be recorded and not pending, and `transfer` a timeline semaphore of the
	/// device.
	pub unsafe fn submit_and_present(
		&self,
		device: &Device,
		swapchain: vk::SwapchainKHR,
		commands: vk::CommandBuffer,
		transfer: vk::Semaphore,
		transfer_value: u64,
	) -> Result<vk::Result, vk::Result>
	{
		let image_index = self.fields.image_index.load(Ordering::Relaxed);
		let submit_semaphore = self.submit_semaphores[image_index as usize];

		let waits = [
			SubmitWait {
				semaphore: self.frame().image_available,
				stages: vk::PipelineStageFlags::ALL_GRAPHICS | vk::PipelineStageFlags::ALL_COMMANDS,
				value: 0,
			},
			SubmitWait {
				semaphore: transfer,
				stages: vk::PipelineStageFlags::ALL_COMMANDS,
				value: transfer_value,
			},
		];

		let signals = [SubmitSignal {
			semaphore: submit_semaphore,
			value: 0,
		}];

		// SAFETY: guaranteed by the caller, and the fence was readied by `begin_frame`.
		unsafe {
			device.queue_submit(
				QueueKind::Graphics,
				&waits,
				&[commands],
				&signals,
				self.frame().in_flight,
			)
		}?;

		Ok(device.queue_present(swapchain, submit_semaphore, image_index))
	}

	/// Moves on to the next frame slot.
	pub fn end_frame(&self)
	{
		let elapsed = self
			.fields
			.elapsed
			.fetch_add(1, Ordering::Relaxed)
			.wrapping_add(1);

		self.fields
			.frame_number
			.store(elapsed % self.frames.len() as u32, Ordering::Relaxed);
	}

	/// # Safety
	///
	/// The device must be the one the loop was created with, and nothing may be using its objects.
	pub unsafe fn destroy(self, device: &Device)
	{
		for frame in &self.frames {
			// SAFETY: guaranteed by the caller.
			unsafe {
				device.destroy_semaphore(frame.image_available);
				device.destroy_fence(frame.in_flight);
			}
		}

		for semaphore in &self.submit_semaphores {
			// SAFETY: guaranteed by the caller.
			unsafe { device.destroy_semaphore(*semaphore) };
		}
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
		assert_eq!(offset_of!(FrameLoop, fields), 0);
		assert_eq!(size_of::<FrameLoopFields>(), 12);
		assert_eq!(offset_of!(FrameLoopFields, frame_number), 0);
		assert_eq!(offset_of!(FrameLoopFields, elapsed), 4);
		assert_eq!(offset_of!(FrameLoopFields, image_index), 8);
	}
}
