use ash::prelude::VkResult;
use ash::vk;

use crate::Device;

#[derive(Clone, Copy, Debug)]
pub struct Attachment
{
	pub description: vk::AttachmentDescription,
	pub is_depth: bool,
}

pub fn subpass_dependencies() -> [vk::SubpassDependency; 2]
{
	[
		vk::SubpassDependency::default()
			.src_subpass(0)
			.dst_subpass(vk::SUBPASS_EXTERNAL)
			.src_stage_mask(
				vk::PipelineStageFlags::COLOR_ATTACHMENT_OUTPUT
					| vk::PipelineStageFlags::LATE_FRAGMENT_TESTS,
			)
			.dst_stage_mask(vk::PipelineStageFlags::FRAGMENT_SHADER)
			.src_access_mask(
				vk::AccessFlags::COLOR_ATTACHMENT_WRITE
					| vk::AccessFlags::DEPTH_STENCIL_ATTACHMENT_WRITE,
			)
			.dst_access_mask(vk::AccessFlags::SHADER_READ)
			.dependency_flags(vk::DependencyFlags::BY_REGION),
		vk::SubpassDependency::default()
			.src_subpass(vk::SUBPASS_EXTERNAL)
			.dst_subpass(0)
			.src_stage_mask(
				vk::PipelineStageFlags::FRAGMENT_SHADER
					| vk::PipelineStageFlags::LATE_FRAGMENT_TESTS,
			)
			.dst_stage_mask(
				vk::PipelineStageFlags::COLOR_ATTACHMENT_OUTPUT
					| vk::PipelineStageFlags::LATE_FRAGMENT_TESTS
					| vk::PipelineStageFlags::EARLY_FRAGMENT_TESTS,
			)
			.src_access_mask(
				vk::AccessFlags::SHADER_READ | vk::AccessFlags::DEPTH_STENCIL_ATTACHMENT_WRITE,
			)
			.dst_access_mask(
				vk::AccessFlags::COLOR_ATTACHMENT_WRITE
					| vk::AccessFlags::DEPTH_STENCIL_ATTACHMENT_WRITE
					| vk::AccessFlags::DEPTH_STENCIL_ATTACHMENT_READ,
			)
			.dependency_flags(vk::DependencyFlags::BY_REGION),
	]
}

pub fn attachment_references(
	attachments: &[Attachment],
) -> (
	Vec<vk::AttachmentReference>,
	Option<vk::AttachmentReference>,
)
{
	let mut colors = Vec::new();
	let mut depth = None;

	for (index, attachment) in attachments.iter().enumerate() {
		if attachment.is_depth {
			depth = Some(vk::AttachmentReference {
				attachment: index as u32,
				layout: vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			});
		} else {
			colors.push(vk::AttachmentReference {
				attachment: index as u32,
				layout: vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
			});
		}
	}

	(colors, depth)
}

impl Device
{
	pub fn create_render_pass(&self, attachments: &[Attachment]) -> VkResult<vk::RenderPass>
	{
		let (colors, depth) = attachment_references(attachments);
		let descriptions: Vec<_> = attachments.iter().map(|a| a.description).collect();

		let mut subpass = vk::SubpassDescription::default()
			.pipeline_bind_point(vk::PipelineBindPoint::GRAPHICS)
			.color_attachments(&colors);

		if let Some(depth) = depth.as_ref() {
			subpass = subpass.depth_stencil_attachment(depth);
		}

		let subpasses = [subpass];
		let dependencies = subpass_dependencies();

		let info = vk::RenderPassCreateInfo::default()
			.attachments(&descriptions)
			.subpasses(&subpasses)
			.dependencies(&dependencies);

		// SAFETY: the create info and everything it points to outlive the call.
		unsafe { self.raw().create_render_pass(&info, None) }
	}

	/// # Safety
	///
	/// The render pass must belong to this device and not be in use.
	pub unsafe fn destroy_render_pass(&self, pass: vk::RenderPass)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_render_pass(pass, None) };
	}

	/// # Safety
	///
	/// `cmd` must be recording outside of a render pass, and the pass and framebuffer must be
	/// compatible and live.
	pub unsafe fn cmd_begin_render_pass(
		&self,
		cmd: vk::CommandBuffer,
		pass: vk::RenderPass,
		framebuffer: vk::Framebuffer,
		render_area: vk::Rect2D,
		clear_values: &[vk::ClearValue],
	)
	{
		let info = vk::RenderPassBeginInfo::default()
			.render_pass(pass)
			.framebuffer(framebuffer)
			.render_area(render_area)
			.clear_values(clear_values);

		// SAFETY: guaranteed by the caller.
		unsafe {
			self.raw()
				.cmd_begin_render_pass(cmd, &info, vk::SubpassContents::INLINE)
		};
	}

	/// # Safety
	///
	/// `cmd` must be inside a render pass.
	pub unsafe fn cmd_end_render_pass(&self, cmd: vk::CommandBuffer)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_end_render_pass(cmd) };
	}

	pub fn create_framebuffer(
		&self,
		pass: vk::RenderPass,
		views: &[vk::ImageView],
		size: (u32, u32),
	) -> VkResult<vk::Framebuffer>
	{
		let info = vk::FramebufferCreateInfo::default()
			.render_pass(pass)
			.attachments(views)
			.width(size.0)
			.height(size.1)
			.layers(1);

		// SAFETY: the render pass and views belong to this device.
		unsafe { self.raw().create_framebuffer(&info, None) }
	}

	/// # Safety
	///
	/// The framebuffer must belong to this device and not be in use.
	pub unsafe fn destroy_framebuffer(&self, framebuffer: vk::Framebuffer)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_framebuffer(framebuffer, None) };
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn attachment(is_depth: bool) -> Attachment
	{
		Attachment {
			description: vk::AttachmentDescription::default(),
			is_depth,
		}
	}

	#[test]
	fn colour_and_depth_references_follow_attachment_order()
	{
		let (colors, depth) =
			attachment_references(&[attachment(false), attachment(true), attachment(false)]);

		assert_eq!(colors.len(), 2);
		assert_eq!(colors[0].attachment, 0);
		assert_eq!(colors[1].attachment, 2);
		assert_eq!(colors[0].layout, vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL);

		let depth = depth.unwrap();
		assert_eq!(depth.attachment, 1);
		assert_eq!(
			depth.layout,
			vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		);
	}

	#[test]
	fn no_depth_attachment_leaves_the_reference_empty()
	{
		let (colors, depth) = attachment_references(&[attachment(false)]);

		assert_eq!(colors.len(), 1);
		assert!(depth.is_none());
	}

	#[test]
	fn the_last_depth_attachment_wins()
	{
		let (_, depth) = attachment_references(&[attachment(true), attachment(true)]);

		assert_eq!(depth.unwrap().attachment, 1);
	}

	#[test]
	fn dependencies_cover_both_directions()
	{
		let [out, into] = subpass_dependencies();

		assert_eq!(out.src_subpass, 0);
		assert_eq!(out.dst_subpass, vk::SUBPASS_EXTERNAL);
		assert_eq!(into.src_subpass, vk::SUBPASS_EXTERNAL);
		assert_eq!(into.dst_subpass, 0);
		assert!(
			into.dst_access_mask
				.contains(vk::AccessFlags::DEPTH_STENCIL_ATTACHMENT_READ)
		);
		assert!(
			out.dependency_flags
				.contains(vk::DependencyFlags::BY_REGION)
		);
	}
}
