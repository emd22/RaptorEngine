use ash::vk;

use crate::{Device, Level};

struct LayoutSync
{
	stage: vk::PipelineStageFlags2,
	access: vk::AccessFlags2,
}

fn layout_sync(layout: vk::ImageLayout) -> Option<LayoutSync>
{
	let (stage, access) = match layout {
		vk::ImageLayout::UNDEFINED => {
			(vk::PipelineStageFlags2::TOP_OF_PIPE, vk::AccessFlags2::NONE)
		}
		vk::ImageLayout::TRANSFER_DST_OPTIMAL => (
			vk::PipelineStageFlags2::ALL_TRANSFER,
			vk::AccessFlags2::TRANSFER_WRITE,
		),
		vk::ImageLayout::TRANSFER_SRC_OPTIMAL => (
			vk::PipelineStageFlags2::ALL_TRANSFER,
			vk::AccessFlags2::TRANSFER_READ,
		),
		vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL => (
			vk::PipelineStageFlags2::FRAGMENT_SHADER,
			vk::AccessFlags2::SHADER_READ,
		),
		vk::ImageLayout::DEPTH_STENCIL_READ_ONLY_OPTIMAL => (
			vk::PipelineStageFlags2::EARLY_FRAGMENT_TESTS
				| vk::PipelineStageFlags2::LATE_FRAGMENT_TESTS,
			vk::AccessFlags2::DEPTH_STENCIL_ATTACHMENT_READ,
		),
		vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL => (
			vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT,
			vk::AccessFlags2::COLOR_ATTACHMENT_WRITE,
		),
		vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL => (
			vk::PipelineStageFlags2::EARLY_FRAGMENT_TESTS
				| vk::PipelineStageFlags2::LATE_FRAGMENT_TESTS,
			vk::AccessFlags2::DEPTH_STENCIL_ATTACHMENT_READ
				| vk::AccessFlags2::DEPTH_STENCIL_ATTACHMENT_WRITE,
		),
		vk::ImageLayout::PRESENT_SRC_KHR => (
			vk::PipelineStageFlags2::BOTTOM_OF_PIPE,
			vk::AccessFlags2::NONE,
		),
		_ => return None,
	};

	Some(LayoutSync { stage, access })
}

#[derive(Clone, Copy, Debug)]
pub struct LayoutTransition
{
	pub image: vk::Image,
	pub aspect: vk::ImageAspectFlags,
	pub old_layout: vk::ImageLayout,
	pub new_layout: vk::ImageLayout,
	pub base_mip: u32,
	pub levels: u32,
	pub cmd_queue_family: u32,
}

impl LayoutTransition
{
	pub fn barrier(
		&self,
		graphics_family: u32,
		unknown: &mut bool,
	) -> vk::ImageMemoryBarrier2<'static>
	{
		let mut lookup = |layout| {
			layout_sync(layout).unwrap_or_else(|| {
				*unknown = true;
				LayoutSync {
					stage: vk::PipelineStageFlags2::NONE,
					access: vk::AccessFlags2::NONE,
				}
			})
		};

		let src = lookup(self.old_layout);
		let dst = lookup(self.new_layout);

		let mut barrier = vk::ImageMemoryBarrier2::default()
			.src_stage_mask(src.stage)
			.src_access_mask(src.access)
			.dst_stage_mask(dst.stage)
			.dst_access_mask(dst.access)
			.old_layout(self.old_layout)
			.new_layout(self.new_layout)
			.src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
			.dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
			.image(self.image)
			.subresource_range(
				vk::ImageSubresourceRange::default()
					.aspect_mask(self.aspect)
					.base_mip_level(self.base_mip)
					.level_count(self.levels)
					.layer_count(vk::REMAINING_ARRAY_LAYERS),
			);

		if self.cmd_queue_family != graphics_family {
			barrier.src_stage_mask &= vk::PipelineStageFlags2::ALL_TRANSFER;
			barrier.dst_stage_mask &= vk::PipelineStageFlags2::ALL_TRANSFER;

			if barrier.src_stage_mask == vk::PipelineStageFlags2::NONE {
				barrier.src_access_mask = vk::AccessFlags2::NONE;
			}

			if barrier.dst_stage_mask == vk::PipelineStageFlags2::NONE {
				barrier.dst_access_mask = vk::AccessFlags2::NONE;
			}
		}

		barrier
	}
}

impl Device
{
	/// # Safety
	///
	/// `cmd` must be recording on a queue family that supports the stages used, and
	/// `transition.image` must be live.
	pub unsafe fn cmd_image_layout_transition(
		&self,
		cmd: vk::CommandBuffer,
		transition: &LayoutTransition,
	)
	{
		let mut unknown = false;
		let barrier = transition.barrier(self.families().graphics, &mut unknown);

		if unknown {
			self.log().log(Level::Error, "Unknown image layout!");
		}

		let barriers = [barrier];
		let dependency = vk::DependencyInfo::default().image_memory_barriers(&barriers);

		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_pipeline_barrier2(cmd, &dependency) };
	}

	/// # Safety
	///
	/// `cmd` must be recording on the transfer queue family, and the image must be live.
	pub unsafe fn cmd_image_transfer_release(
		&self,
		cmd: vk::CommandBuffer,
		image: vk::Image,
		aspect: vk::ImageAspectFlags,
		mips: u32,
	) -> bool
	{
		let families = self.families();

		if !families.has_independent_transfer() {
			return false;
		}

		let barrier = vk::ImageMemoryBarrier2::default()
			.src_stage_mask(vk::PipelineStageFlags2::ALL_TRANSFER)
			.src_access_mask(vk::AccessFlags2::TRANSFER_WRITE)
			.dst_stage_mask(vk::PipelineStageFlags2::NONE)
			.dst_access_mask(vk::AccessFlags2::NONE)
			.old_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
			.new_layout(vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL)
			.src_queue_family_index(families.transfer)
			.dst_queue_family_index(families.graphics)
			.image(image)
			.subresource_range(whole_image(aspect, mips));

		let barriers = [barrier];
		let dependency = vk::DependencyInfo::default().image_memory_barriers(&barriers);

		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_pipeline_barrier2(cmd, &dependency) };

		true
	}

	/// # Safety
	///
	/// `cmd` must be recording on the graphics queue family, and the image must be live.
	pub unsafe fn cmd_image_graphics_acquire(
		&self,
		cmd: vk::CommandBuffer,
		image: vk::Image,
		aspect: vk::ImageAspectFlags,
		mips: u32,
	) -> bool
	{
		let families = self.families();

		if !families.has_independent_transfer() {
			return false;
		}

		self.log().log(Level::Info, "Graphics acquire");

		let barrier = vk::ImageMemoryBarrier2::default()
			.src_stage_mask(vk::PipelineStageFlags2::NONE)
			.src_access_mask(vk::AccessFlags2::NONE)
			.dst_stage_mask(vk::PipelineStageFlags2::FRAGMENT_SHADER)
			.dst_access_mask(vk::AccessFlags2::SHADER_READ)
			.old_layout(vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL)
			.new_layout(vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL)
			.src_queue_family_index(families.transfer)
			.dst_queue_family_index(families.graphics)
			.image(image)
			.subresource_range(whole_image(aspect, mips));

		let barriers = [barrier];
		let dependency = vk::DependencyInfo::default().image_memory_barriers(&barriers);

		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_pipeline_barrier2(cmd, &dependency) };

		true
	}

	/// # Safety
	///
	/// `cmd` must be recording and the buffer live.
	pub unsafe fn cmd_buffer_compute_to_fragment(
		&self,
		cmd: vk::CommandBuffer,
		buffer: vk::Buffer,
		size: u64,
	)
	{
		let barrier = vk::BufferMemoryBarrier2::default()
			.src_stage_mask(vk::PipelineStageFlags2::COMPUTE_SHADER)
			.src_access_mask(vk::AccessFlags2::SHADER_WRITE)
			.dst_stage_mask(vk::PipelineStageFlags2::FRAGMENT_SHADER)
			.dst_access_mask(vk::AccessFlags2::SHADER_READ)
			.src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
			.dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
			.buffer(buffer)
			.size(size);

		let barriers = [barrier];
		let dependency = vk::DependencyInfo::default().buffer_memory_barriers(&barriers);

		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_pipeline_barrier2(cmd, &dependency) };
	}

	/// # Safety
	///
	/// `cmd` must be recording and the buffer live.
	pub unsafe fn cmd_buffer_fragment_to_compute(
		&self,
		cmd: vk::CommandBuffer,
		buffer: vk::Buffer,
		size: u64,
	)
	{
		let barrier = vk::BufferMemoryBarrier2::default()
			.src_stage_mask(vk::PipelineStageFlags2::FRAGMENT_SHADER)
			.src_access_mask(vk::AccessFlags2::NONE)
			.dst_stage_mask(vk::PipelineStageFlags2::COMPUTE_SHADER)
			.dst_access_mask(vk::AccessFlags2::NONE)
			.src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
			.dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
			.buffer(buffer)
			.size(size);

		let barriers = [barrier];
		let dependency = vk::DependencyInfo::default().buffer_memory_barriers(&barriers);

		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_pipeline_barrier2(cmd, &dependency) };
	}
}

fn whole_image(aspect: vk::ImageAspectFlags, mips: u32) -> vk::ImageSubresourceRange
{
	vk::ImageSubresourceRange::default()
		.aspect_mask(aspect)
		.level_count(mips)
		.layer_count(1)
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn transition(old: vk::ImageLayout, new: vk::ImageLayout, family: u32) -> LayoutTransition
	{
		LayoutTransition {
			image: vk::Image::null(),
			aspect: vk::ImageAspectFlags::COLOR,
			old_layout: old,
			new_layout: new,
			base_mip: 2,
			levels: 3,
			cmd_queue_family: family,
		}
	}

	#[test]
	fn upload_to_sample_on_the_graphics_queue_keeps_every_stage()
	{
		let mut unknown = false;
		let barrier = transition(
			vk::ImageLayout::TRANSFER_DST_OPTIMAL,
			vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			0,
		)
		.barrier(0, &mut unknown);

		assert!(!unknown);
		assert_eq!(
			barrier.src_stage_mask,
			vk::PipelineStageFlags2::ALL_TRANSFER
		);
		assert_eq!(
			barrier.dst_stage_mask,
			vk::PipelineStageFlags2::FRAGMENT_SHADER
		);
		assert_eq!(barrier.dst_access_mask, vk::AccessFlags2::SHADER_READ);
		assert_eq!(barrier.subresource_range.base_mip_level, 2);
		assert_eq!(barrier.subresource_range.level_count, 3);
		assert_eq!(
			barrier.subresource_range.layer_count,
			vk::REMAINING_ARRAY_LAYERS
		);
	}

	#[test]
	fn transfer_queue_barriers_drop_consumer_stages()
	{
		let mut unknown = false;
		let barrier = transition(
			vk::ImageLayout::TRANSFER_DST_OPTIMAL,
			vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			1,
		)
		.barrier(0, &mut unknown);

		assert_eq!(
			barrier.src_stage_mask,
			vk::PipelineStageFlags2::ALL_TRANSFER
		);
		assert_eq!(barrier.src_access_mask, vk::AccessFlags2::TRANSFER_WRITE);
		assert_eq!(barrier.dst_stage_mask, vk::PipelineStageFlags2::NONE);
		assert_eq!(barrier.dst_access_mask, vk::AccessFlags2::NONE);
	}

	#[test]
	fn transfer_queue_undefined_source_has_no_access()
	{
		let mut unknown = false;
		let barrier = transition(
			vk::ImageLayout::UNDEFINED,
			vk::ImageLayout::TRANSFER_DST_OPTIMAL,
			1,
		)
		.barrier(0, &mut unknown);

		assert_eq!(barrier.src_stage_mask, vk::PipelineStageFlags2::NONE);
		assert_eq!(barrier.src_access_mask, vk::AccessFlags2::NONE);
		assert_eq!(
			barrier.dst_stage_mask,
			vk::PipelineStageFlags2::ALL_TRANSFER
		);
	}

	#[test]
	fn unknown_layouts_are_flagged_and_empty()
	{
		let mut unknown = false;
		let barrier = transition(
			vk::ImageLayout::GENERAL,
			vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
			0,
		)
		.barrier(0, &mut unknown);

		assert!(unknown);
		assert_eq!(barrier.src_stage_mask, vk::PipelineStageFlags2::NONE);
		assert_eq!(
			barrier.dst_stage_mask,
			vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT
		);
	}
}
