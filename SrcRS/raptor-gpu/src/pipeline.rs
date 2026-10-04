use ash::prelude::VkResult;
use ash::vk;

use crate::Device;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PushConstantDef
{
	pub size: u32,
	pub stages: vk::ShaderStageFlags,
}

pub fn push_constant_ranges(defs: &[PushConstantDef]) -> Vec<vk::PushConstantRange>
{
	let mut offset = 0;
	let mut ranges = Vec::with_capacity(defs.len());

	for def in defs {
		if def.stages.is_empty() {
			continue;
		}

		ranges.push(vk::PushConstantRange {
			stage_flags: def.stages,
			offset,
			size: def.size,
		});

		offset += def.size;
	}

	ranges
}

pub fn is_depth_format(format: vk::Format) -> bool
{
	(vk::Format::D16_UNORM.as_raw()..=vk::Format::D32_SFLOAT_S8_UINT.as_raw())
		.contains(&format.as_raw())
}

pub struct GraphicsPipelineDesc<'a>
{
	pub stages: &'a [(vk::ShaderStageFlags, vk::ShaderModule)],
	pub vertex_binding: Option<vk::VertexInputBindingDescription>,
	pub vertex_attributes: &'a [vk::VertexInputAttributeDescription],
	pub color_blend: &'a [vk::PipelineColorBlendAttachmentState],
	pub attachment_formats: &'a [vk::Format],
	pub cull_mode: vk::CullModeFlags,
	pub front_face: vk::FrontFace,
	pub polygon_mode: vk::PolygonMode,
	pub depth_compare_op: vk::CompareOp,
	pub render_lines: bool,
	pub disable_depth_test: bool,
	pub disable_depth_write: bool,
	pub layout: vk::PipelineLayout,
	pub render_pass: vk::RenderPass,
}

impl Device
{
	pub fn create_pipeline_layout(
		&self,
		set_layouts: &[vk::DescriptorSetLayout],
		push_constants: &[PushConstantDef],
	) -> VkResult<vk::PipelineLayout>
	{
		let ranges = push_constant_ranges(push_constants);

		let info = vk::PipelineLayoutCreateInfo::default()
			.set_layouts(set_layouts)
			.push_constant_ranges(&ranges);

		// SAFETY: the create info and everything it points to outlive the call.
		unsafe { self.raw().create_pipeline_layout(&info, None) }
	}

	/// # Safety
	///
	/// The layout must belong to this device and not be in use.
	pub unsafe fn destroy_pipeline_layout(&self, layout: vk::PipelineLayout)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_pipeline_layout(layout, None) };
	}

	pub fn create_shader_module(&self, code: &[u32]) -> VkResult<vk::ShaderModule>
	{
		let info = vk::ShaderModuleCreateInfo::default().code(code);

		// SAFETY: the create info outlives the call.
		unsafe { self.raw().create_shader_module(&info, None) }
	}

	/// # Safety
	///
	/// The module must belong to this device.
	pub unsafe fn destroy_shader_module(&self, module: vk::ShaderModule)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_shader_module(module, None) };
	}

	pub fn create_graphics_pipeline(&self, desc: &GraphicsPipelineDesc) -> VkResult<vk::Pipeline>
	{
		let has_depth = desc
			.attachment_formats
			.iter()
			.any(|format| is_depth_format(*format));

		let stages: Vec<_> = desc
			.stages
			.iter()
			.map(|(stage, module)| {
				vk::PipelineShaderStageCreateInfo::default()
					.stage(*stage)
					.module(*module)
					.name(c"main")
			})
			.collect();

		let dynamic_states = [
			vk::DynamicState::VIEWPORT,
			vk::DynamicState::SCISSOR,
			vk::DynamicState::CULL_MODE,
		];
		let dynamic_state =
			vk::PipelineDynamicStateCreateInfo::default().dynamic_states(&dynamic_states);

		let viewport_state = vk::PipelineViewportStateCreateInfo::default()
			.viewport_count(1)
			.scissor_count(1);

		let bindings: Vec<_> = desc.vertex_binding.into_iter().collect();
		let vertex_input = vk::PipelineVertexInputStateCreateInfo::default()
			.vertex_binding_descriptions(&bindings)
			.vertex_attribute_descriptions(if bindings.is_empty() {
				&[]
			} else {
				desc.vertex_attributes
			});

		let input_assembly = vk::PipelineInputAssemblyStateCreateInfo::default()
			.topology(if desc.render_lines {
				vk::PrimitiveTopology::LINE_LIST
			} else {
				vk::PrimitiveTopology::TRIANGLE_LIST
			})
			.primitive_restart_enable(false);

		let rasterizer = vk::PipelineRasterizationStateCreateInfo::default()
			.depth_clamp_enable(false)
			.rasterizer_discard_enable(false)
			.polygon_mode(desc.polygon_mode)
			.cull_mode(desc.cull_mode)
			.front_face(desc.front_face)
			.depth_bias_enable(false)
			.line_width(1.0);

		let multisampling = vk::PipelineMultisampleStateCreateInfo::default()
			.rasterization_samples(vk::SampleCountFlags::TYPE_1)
			.sample_shading_enable(false);

		let color_blend = vk::PipelineColorBlendStateCreateInfo::default()
			.logic_op_enable(false)
			.attachments(desc.color_blend);

		let depth_stencil = vk::PipelineDepthStencilStateCreateInfo::default()
			.depth_test_enable(!desc.disable_depth_test)
			.depth_write_enable(!desc.disable_depth_write)
			.depth_compare_op(desc.depth_compare_op)
			.depth_bounds_test_enable(false)
			.stencil_test_enable(false);

		let mut info = vk::GraphicsPipelineCreateInfo::default()
			.stages(&stages)
			.vertex_input_state(&vertex_input)
			.input_assembly_state(&input_assembly)
			.viewport_state(&viewport_state)
			.rasterization_state(&rasterizer)
			.multisample_state(&multisampling)
			.color_blend_state(&color_blend)
			.dynamic_state(&dynamic_state)
			.layout(desc.layout)
			.render_pass(desc.render_pass)
			.subpass(0);

		if has_depth {
			info = info.depth_stencil_state(&depth_stencil);
		}

		// SAFETY: the create info and everything it points to outlive the call.
		unsafe {
			self.raw()
				.create_graphics_pipelines(vk::PipelineCache::null(), &[info], None)
		}
		.map(|pipelines| pipelines[0])
		.map_err(|(_, result)| result)
	}

	pub fn create_compute_pipeline(
		&self,
		module: vk::ShaderModule,
		layout: vk::PipelineLayout,
	) -> VkResult<vk::Pipeline>
	{
		let stage = vk::PipelineShaderStageCreateInfo::default()
			.stage(vk::ShaderStageFlags::COMPUTE)
			.module(module)
			.name(c"main");

		let info = vk::ComputePipelineCreateInfo::default()
			.stage(stage)
			.layout(layout);

		// SAFETY: the create info and everything it points to outlive the call.
		unsafe {
			self.raw()
				.create_compute_pipelines(vk::PipelineCache::null(), &[info], None)
		}
		.map(|pipelines| pipelines[0])
		.map_err(|(_, result)| result)
	}

	/// # Safety
	///
	/// The pipeline must belong to this device and not be in use.
	pub unsafe fn destroy_pipeline(&self, pipeline: vk::Pipeline)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().destroy_pipeline(pipeline, None) };
	}

	/// # Safety
	///
	/// `cmd` must be recording, and the pipeline must be live and usable with the bind point.
	pub unsafe fn cmd_bind_pipeline(
		&self,
		cmd: vk::CommandBuffer,
		bind_point: vk::PipelineBindPoint,
		pipeline: vk::Pipeline,
	)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_bind_pipeline(cmd, bind_point, pipeline) };
	}

	/// # Safety
	///
	/// `cmd` must be recording with a graphics pipeline that has cull mode as dynamic state bound.
	pub unsafe fn cmd_set_cull_mode(&self, cmd: vk::CommandBuffer, mode: vk::CullModeFlags)
	{
		// SAFETY: guaranteed by the caller.
		unsafe { self.raw().cmd_set_cull_mode(cmd, mode) };
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn push_constant_ranges_stack_their_offsets_and_skip_empty_stage_sets()
	{
		let ranges = push_constant_ranges(&[
			PushConstantDef {
				size: 64,
				stages: vk::ShaderStageFlags::VERTEX,
			},
			PushConstantDef {
				size: 16,
				stages: vk::ShaderStageFlags::empty(),
			},
			PushConstantDef {
				size: 32,
				stages: vk::ShaderStageFlags::FRAGMENT,
			},
		]);

		assert_eq!(ranges.len(), 2);
		assert_eq!((ranges[0].offset, ranges[0].size), (0, 64));
		assert_eq!((ranges[1].offset, ranges[1].size), (64, 32));
		assert_eq!(ranges[1].stage_flags, vk::ShaderStageFlags::FRAGMENT);
	}

	#[test]
	fn depth_formats_are_the_vulkan_depth_range()
	{
		assert!(is_depth_format(vk::Format::D16_UNORM));
		assert!(is_depth_format(vk::Format::D32_SFLOAT));
		assert!(is_depth_format(vk::Format::D32_SFLOAT_S8_UINT));
		assert!(!is_depth_format(vk::Format::R8G8B8A8_UNORM));
		assert!(!is_depth_format(vk::Format::R16G16B16A16_SFLOAT));
		assert!(!is_depth_format(vk::Format::UNDEFINED));
	}
}
