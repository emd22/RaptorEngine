use std::sync::Arc;

use ash::prelude::VkResult;
use ash::vk::{self, Handle};

use crate::{
	Allocator, Attachment, Device, ImageFormat, ImageRecord, TargetConfig, TargetList, clear_values,
};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct RenderStageFields
{
	pub render_pass: u64,
	pub width: u32,
	pub height: u32,
	pub offset_x: u32,
	pub offset_y: u32,
}

/// The swapchain images the final stage draws to: a view of each, and their size.
pub struct FinalViews<'a>
{
	pub views: &'a [vk::ImageView],
	pub size: (u32, u32),
}

/// A render stage: the targets it draws to, the render pass over them, the framebuffers for it
/// (one, or one per swapchain image for the final stage), and its clear values.
#[repr(C)]
pub struct RenderStageRecord
{
	pub fields: RenderStageFields,
	targets: TargetList,
	framebuffers: Vec<vk::Framebuffer>,
	clear_values: Vec<vk::ClearValue>,
}

impl RenderStageRecord
{
	pub fn new() -> Box<Self>
	{
		Box::new(Self {
			fields: RenderStageFields {
				render_pass: 0,
				width: 0,
				height: 0,
				offset_x: 0,
				offset_y: 0,
			},
			targets: TargetList::default(),
			framebuffers: Vec::new(),
			clear_values: Vec::new(),
		})
	}

	pub fn targets(&self) -> &TargetList
	{
		&self.targets
	}

	/// Adds a target. See `TargetList::add`.
	pub fn add_target(
		&mut self,
		config: TargetConfig,
		format: ImageFormat,
		size: (u32, u32),
		reference: Option<Arc<ImageRecord>>,
	) -> usize
	{
		self.targets.add(config, format, size, reference)
	}

	/// The attachments of the render pass.
	pub fn descriptions(&mut self) -> &[vk::AttachmentDescription]
	{
		self.targets.descriptions()
	}

	/// Makes the stage's images, render pass and framebuffers, or makes them again at a new size if
	/// `recreate`. The final stage draws to `final_views` instead of to images of its own. The
	/// first build also works out the clear values.
	///
	/// # Safety
	///
	/// The device and allocator must be live and belong together, and nothing the stage made before
	/// may be in use. Everything in `final_views` must be live.
	pub unsafe fn build(
		&mut self,
		device: &Device,
		allocator: &Allocator,
		size: (u32, u32),
		final_views: Option<FinalViews>,
		recreate: bool,
	) -> VkResult<()>
	{
		// SAFETY: guaranteed by the caller.
		unsafe {
			if recreate {
				self.targets.recreate_images(device, allocator, size)?;
			} else {
				self.targets.create_images(device, allocator, size)?;
			}

			self.destroy_framebuffers(device);
		}

		if !recreate && self.clear_values.is_empty() {
			self.clear_values = clear_values(&self.targets.clear_targets());
		}

		let descriptions = self.targets.descriptions().to_vec();

		let attachments: Vec<_> = descriptions
			.iter()
			.zip(self.targets.iter())
			.map(|(description, target)| Attachment {
				description: *description,
				is_depth: target.config.is_depth(),
			})
			.collect();

		// SAFETY: guaranteed by the caller.
		unsafe { self.create_pass(device, &attachments, size, (0, 0)) }?;

		let view_sets: Vec<Vec<vk::ImageView>> = match final_views.as_ref() {
			Some(final_views) => final_views.views.iter().map(|view| vec![*view]).collect(),
			None => vec![self.targets.views().to_vec()],
		};

		let framebuffer_size = final_views.map_or(size, |final_views| final_views.size);

		// SAFETY: guaranteed by the caller.
		unsafe { self.create_framebuffers(device, &view_sets, framebuffer_size) }
	}

	/// # Safety
	///
	/// The device must be the one the stage was created with, and the old render pass must not be
	/// in use.
	unsafe fn create_pass(
		&mut self,
		device: &Device,
		attachments: &[Attachment],
		size: (u32, u32),
		offset: (u32, u32),
	) -> VkResult<()>
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.destroy_pass(device) };

		let pass = device.create_render_pass(attachments)?;

		self.fields = RenderStageFields {
			render_pass: pass.as_raw(),
			width: size.0,
			height: size.1,
			offset_x: offset.0,
			offset_y: offset.1,
		};

		Ok(())
	}

	/// # Safety
	///
	/// The device must be the one the stage was created with, and the render pass must not be in
	/// use.
	unsafe fn destroy_pass(&mut self, device: &Device)
	{
		if self.fields.render_pass != 0 {
			// SAFETY: guaranteed by the caller.
			unsafe {
				device.destroy_render_pass(vk::RenderPass::from_raw(self.fields.render_pass))
			};
			self.fields.render_pass = 0;
		}
	}

	/// Makes a framebuffer for each set of views.
	///
	/// # Safety
	///
	/// As for `create_pass`, and the views must be compatible with the render pass.
	unsafe fn create_framebuffers(
		&mut self,
		device: &Device,
		view_sets: &[Vec<vk::ImageView>],
		size: (u32, u32),
	) -> VkResult<()>
	{
		for views in view_sets {
			let framebuffer = device.create_framebuffer(
				vk::RenderPass::from_raw(self.fields.render_pass),
				views,
				size,
			)?;

			self.framebuffers.push(framebuffer);
		}

		Ok(())
	}

	/// # Safety
	///
	/// The device must be the one the stage was created with, and the framebuffers must not be in
	/// use.
	unsafe fn destroy_framebuffers(&mut self, device: &Device)
	{
		for framebuffer in self.framebuffers.drain(..) {
			// SAFETY: guaranteed by the caller.
			unsafe { device.destroy_framebuffer(framebuffer) };
		}
	}

	/// Destroys everything the stage made, including its images.
	///
	/// # Safety
	///
	/// The device and allocator must be the ones the stage was made with, and nothing may be using
	/// them.
	pub unsafe fn destroy(mut self, device: &Device, allocator: &Allocator)
	{
		// SAFETY: guaranteed by the caller.
		unsafe {
			self.destroy_framebuffers(device);
			self.destroy_pass(device);
			self.targets.release(device, allocator);
		}
	}

	/// Begins the render pass over `render_area`, then points the viewport and scissor at
	/// `draw_area`. A final stage picks the framebuffer for `image_index`.
	///
	/// # Safety
	///
	/// `cmd` must be recording outside of a render pass, and the stage must have been built.
	pub unsafe fn begin(
		&self,
		device: &Device,
		cmd: vk::CommandBuffer,
		image_index: u32,
		render_area: vk::Rect2D,
		draw_area: vk::Rect2D,
	)
	{
		let framebuffer = if self.framebuffers.len() > 1 {
			self.framebuffers[image_index as usize]
		} else {
			self.framebuffers[0]
		};

		// SAFETY: guaranteed by the caller.
		unsafe {
			device.cmd_begin_render_pass(
				cmd,
				vk::RenderPass::from_raw(self.fields.render_pass),
				framebuffer,
				render_area,
				&self.clear_values,
			);
			device.cmd_set_viewport_scissor(cmd, draw_area);
		}
	}

	/// Ends the render pass. It transitions every attachment to its final layout, so the layouts
	/// that the images are tracked with have to follow, or the next explicit barrier on one
	/// would report a stale old layout. Images that do not exist are skipped.
	///
	/// # Safety
	///
	/// `cmd` must be inside the stage's render pass.
	pub unsafe fn end(&self, device: &Device, cmd: vk::CommandBuffer)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { device.cmd_end_render_pass(cmd) };

		for target in self.targets.iter() {
			if target.image.fields().image != 0 {
				target.image.set_layout(target.config.final_layout);
			}
		}
	}
}

#[cfg(test)]
mod tests
{
	use std::mem::{offset_of, size_of};

	use super::*;
	use crate::ImageFormat;

	#[test]
	fn the_public_fields_have_the_layout_the_c_header_declares()
	{
		assert_eq!(offset_of!(RenderStageRecord, fields), 0);
		assert_eq!(size_of::<RenderStageFields>(), 24);
		assert_eq!(offset_of!(RenderStageFields, width), 8);
		assert_eq!(offset_of!(RenderStageFields, height), 12);
		assert_eq!(offset_of!(RenderStageFields, offset_x), 16);
		assert_eq!(offset_of!(RenderStageFields, offset_y), 20);
	}

	#[test]
	fn adding_targets_to_a_new_stage_lists_them_in_order()
	{
		let mut stage = RenderStageRecord::new();

		let config = TargetConfig {
			image_type: crate::ImageType::Flat,
			usage: vk::ImageUsageFlags::COLOR_ATTACHMENT,
			aspect: vk::ImageAspectFlags::COLOR,
			samples: vk::SampleCountFlags::TYPE_1,
			load_op: vk::AttachmentLoadOp::CLEAR,
			store_op: vk::AttachmentStoreOp::STORE,
			stencil_load_op: vk::AttachmentLoadOp::CLEAR,
			stencil_store_op: vk::AttachmentStoreOp::STORE,
			initial_layout: vk::ImageLayout::UNDEFINED,
			final_layout: vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
			render_pass_only: false,
		};

		assert_eq!(
			stage.add_target(config, ImageFormat::Rgba8UNorm, (8, 8), None),
			0
		);
		assert_eq!(
			stage.add_target(config, ImageFormat::R8UNorm, (8, 8), None),
			1
		);

		assert_eq!(stage.targets().len(), 2);
		assert_eq!(stage.descriptions().len(), 2);
	}
}
