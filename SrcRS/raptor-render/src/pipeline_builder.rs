use std::sync::Arc;

use raptor_gpu::vk::{self, Handle};
use raptor_gpu::{
	DescriptorCache, DescriptorEntryRef, DescriptorResourceRef, DescriptorSetRecord,
	DescriptorSlot, Device, DsLayoutCache, ENTRY_BUFFER, ENTRY_IMAGE, GraphicsPipelineDesc, Level,
	PipelineKey, PipelineLayoutRecord, PipelineRecord, PushConstantDef, RenderStageRecord,
	ShaderProgramRecord, blend_hash, find_missing_descriptor, layout_hash, pass_hash,
	shader_stage_flags,
};

/// What to build a pipeline from. Descriptors are given by the set they go in.
pub struct PipelineBuild<'a>
{
	pub debug_name: &'a str,
	pub shader: u32,
	pub macro_hash: u64,

	pub vertex: Option<&'a ShaderProgramRecord>,
	pub pixel: Option<&'a ShaderProgramRecord>,
	pub compute: Option<&'a ShaderProgramRecord>,

	pub vertex_type: u32,
	pub has_vertex_input: bool,
	pub vertex_binding: Option<vk::VertexInputBindingDescription>,
	pub vertex_attributes: &'a [vk::VertexInputAttributeDescription],

	pub color_blend: &'a [vk::PipelineColorBlendAttachmentState],

	pub cull_mode: vk::CullModeFlags,
	pub front_face: vk::FrontFace,
	pub polygon_mode: vk::PolygonMode,
	pub depth_compare_op: vk::CompareOp,
	pub render_lines: bool,
	pub disable_depth_test: bool,
	pub disable_depth_write: bool,

	/// The size and shader types (`SHADER_*` bits) of each push constant range
	pub push_constants: &'a [(u32, u32)],

	/// The descriptor entries of each set, by the index of the set. Sets without entries are left
	/// out.
	pub sets: &'a [Vec<DescriptorEntryRef>],
}

/// A descriptor set the pipeline binds, and the layout it was made with.
pub struct BuiltSet
{
	pub index: u32,
	pub set: *mut DescriptorSetRecord,
	pub layout: vk::DescriptorSetLayout,
}

pub struct PipelineBuilt
{
	pub pipeline: Box<PipelineRecord>,
	pub layout: Arc<PipelineLayoutRecord>,
	pub sets: Vec<BuiltSet>,
	pub key: PipelineKey,
}

#[derive(Debug, PartialEq, Eq)]
pub enum BuildError
{
	/// A graphics pipeline needs a vertex and a pixel program, and a compute pipeline only a
	/// compute one
	InvalidShaders,
	/// A graphics pipeline draws into a render stage
	NoRenderPass,
	/// A program uses a descriptor that was not declared
	DescriptorMismatch,
	Vulkan(vk::Result),
}

fn slot_kind(resource: &DescriptorResourceRef) -> u32
{
	match resource {
		DescriptorResourceRef::Image { .. } => ENTRY_IMAGE,
		DescriptorResourceRef::Buffer { .. } => ENTRY_BUFFER,
	}
}

fn program_name(bits: u32) -> &'static str
{
	match bits {
		1 => "Vertex",
		2 => "Pixel",
		4 => "Compute",
		_ => "Unknown",
	}
}

/// Checks that everything a program reads was declared. Logs each program that reads something that
/// was not, and returns whether there were any.
fn check_descriptors(device: &Device, build: &PipelineBuild) -> bool
{
	let mut slots = Vec::new();

	for (set, entries) in build.sets.iter().enumerate() {
		for entry in entries {
			slots.push(DescriptorSlot {
				set: set as u32,
				binding: entry.binding,
				kind: slot_kind(&entry.resource),
			});
		}
	}

	let mut failed = false;

	for program in [build.vertex, build.pixel, build.compute]
		.into_iter()
		.flatten()
	{
		let Some(missing) = find_missing_descriptor(program.reflection(), &slots) else {
			continue;
		};

		failed = true;

		let kind = match missing.kind {
			ENTRY_IMAGE => "Image",
			ENTRY_BUFFER => "Buffer",
			_ => "None",
		};

		device.log().log(
			Level::Error,
			&format!(
				"PSOBuild: Missing descriptor (Binding={}, Set={}) of type '{kind}'",
				missing.binding, missing.set
			),
		);
		device.log().log(
			Level::Error,
			&format!(
				"PSOBuild: Failed when builing '{}' ({})",
				// SAFETY: the name points into the program, which is borrowed.
				unsafe { std::ffi::CStr::from_ptr(program.fields.name) }.to_string_lossy(),
				program_name(program.fields.shader_type)
			),
		);
	}

	failed
}

/// Makes the descriptor sets, the pipeline layout and the pipeline, and works out the pipeline's
/// key.
///
/// # Safety
///
/// The device, caches and stage must be live and belong together, every record in the entries must
/// be live, and the programs must have been made on the device.
pub unsafe fn build_pipeline(
	device: &Device,
	descriptor_cache: &mut DescriptorCache,
	ds_layouts: &DsLayoutCache,
	stage: Option<&mut RenderStageRecord>,
	build: &PipelineBuild,
) -> Result<PipelineBuilt, BuildError>
{
	if check_descriptors(device, build) {
		return Err(BuildError::DescriptorMismatch);
	}

	let mut sets = Vec::new();
	let mut set_layouts = Vec::new();

	for (index, entries) in build.sets.iter().enumerate() {
		if entries.is_empty() {
			continue;
		}

		// SAFETY: guaranteed by the caller.
		let (_, set) = unsafe { descriptor_cache.request(device, ds_layouts, entries) }
			.map_err(BuildError::Vulkan)?;

		// SAFETY: the set was just returned by the cache.
		let layout_id = unsafe { (*set).fields.layout_id };

		let layout = ds_layouts
			.get(layout_id)
			.expect("a set was made with a layout that is not there");

		let label = format!("{}_{index}_{}", build.debug_name, entries.len());

		if let Ok(label) = std::ffi::CString::new(label) {
			// SAFETY: the set was just returned by the cache.
			let set_handle = unsafe { (*set).fields.set };

			device.set_object_name(vk::ObjectType::DESCRIPTOR_SET, set_handle, &label);
			device.set_object_name(
				vk::ObjectType::DESCRIPTOR_SET_LAYOUT,
				layout.as_raw(),
				&label,
			);
		}

		set_layouts.push(layout);
		sets.push(BuiltSet {
			index: index as u32,
			set,
			layout,
		});
	}

	let push_constants: Vec<_> = build
		.push_constants
		.iter()
		.map(|(size, shader_types)| PushConstantDef {
			size: *size,
			stages: shader_stage_flags(*shader_types),
		})
		.collect();

	device.log().log(
		Level::Info,
		&format!(
			"Creating pipeline layout with {} descriptors",
			set_layouts.len()
		),
	);

	let layout = PipelineLayoutRecord::create(device, &set_layouts, &push_constants)
		.map_err(BuildError::Vulkan)?;

	let is_compute = build.compute.is_some() && build.vertex.is_none() && build.pixel.is_none();

	let (pipeline, pass_attachments) = if is_compute {
		let compute = build.compute.expect("checked above");

		let pipeline = PipelineRecord::create_compute(
			device,
			vk::ShaderModule::from_raw(compute.fields.module),
			vk::PipelineLayout::from_raw(layout.fields.layout),
		)
		.map_err(BuildError::Vulkan)?;

		device.log().log(
			Level::Info,
			&format!(
				"Creating compute pipeline for shader '{}' -> LayoutHandle={:#x}",
				build.debug_name, layout.fields.layout
			),
		);

		(pipeline, Vec::new())
	} else {
		let (Some(vertex), Some(pixel)) = (build.vertex, build.pixel) else {
			device.log().log(Level::Error, "Invalid shaders provided");
			return Err(BuildError::InvalidShaders);
		};

		let Some(stage) = stage.filter(|stage| stage.fields.render_pass != 0) else {
			device
				.log()
				.log(Level::Error, "No valid renderpass provided");
			return Err(BuildError::NoRenderPass);
		};

		let descriptions = stage.descriptions().to_vec();

		let formats: Vec<_> = descriptions
			.iter()
			.map(|description| description.format)
			.collect();

		let stages = [
			(
				shader_stage_flags(vertex.fields.shader_type),
				vk::ShaderModule::from_raw(vertex.fields.module),
			),
			(
				shader_stage_flags(pixel.fields.shader_type),
				vk::ShaderModule::from_raw(pixel.fields.module),
			),
		];

		let pipeline = PipelineRecord::create_graphics(
			device,
			&GraphicsPipelineDesc {
				stages: &stages,
				vertex_binding: build.vertex_binding,
				vertex_attributes: build.vertex_attributes,
				color_blend: build.color_blend,
				attachment_formats: &formats,
				cull_mode: build.cull_mode,
				front_face: build.front_face,
				polygon_mode: build.polygon_mode,
				depth_compare_op: build.depth_compare_op,
				render_lines: build.render_lines,
				disable_depth_test: build.disable_depth_test,
				disable_depth_write: build.disable_depth_write,
				layout: vk::PipelineLayout::from_raw(layout.fields.layout),
				render_pass: vk::RenderPass::from_raw(stage.fields.render_pass),
			},
		)
		.map_err(BuildError::Vulkan)?;

		device.log().log(
			Level::Info,
			&format!(
				"Creating pipeline for shader '{}' -> LayoutHandle={:#x}",
				build.debug_name, layout.fields.layout
			),
		);

		let attachments: Vec<_> = descriptions
			.iter()
			.map(|description| {
				(
					description.format.as_raw() as u32,
					description.samples.as_raw(),
				)
			})
			.collect();

		(pipeline, attachments)
	};

	if let Ok(label) = std::ffi::CString::new(build.debug_name) {
		device.set_object_name(vk::ObjectType::PIPELINE, pipeline.fields.pipeline, &label);
	}

	let blend_states: Vec<[u32; 8]> = build
		.color_blend
		.iter()
		.map(|state| {
			// SAFETY: the state is eight 32 bit fields, as the hash of it has always taken it.
			unsafe {
				std::mem::transmute::<vk::PipelineColorBlendAttachmentState, [u32; 8]>(*state)
			}
		})
		.collect();

	let layout_sets: Vec<(u32, u32)> = sets
		.iter()
		// SAFETY: the sets were just returned by the cache.
		.map(|set| (set.index, unsafe { (*set.set).fields.layout_id }))
		.collect();

	let key = PipelineKey {
		macro_hash: build.macro_hash,
		blend_hash: if is_compute {
			0
		} else {
			blend_hash(&blend_states)
		},
		pass_hash: if is_compute {
			0
		} else {
			pass_hash(&pass_attachments)
		},
		layout_hash: layout_hash(&layout_sets, build.push_constants),
		shader: build.shader,
		vertex_type: build.vertex_type,
		cull_mode: build.cull_mode.as_raw(),
		winding_order: build.front_face.as_raw(),
		polygon_mode: build.polygon_mode.as_raw(),
		depth_compare_op: build.depth_compare_op.as_raw(),
		is_compute: u8::from(is_compute),
		has_vertex_input: u8::from(build.has_vertex_input),
		depth_test: u8::from(!build.disable_depth_test),
		depth_write: u8::from(!build.disable_depth_write),
		render_lines: u8::from(build.render_lines),
		reserved: [0; 3],
	};

	Ok(PipelineBuilt {
		pipeline,
		layout,
		sets,
		key,
	})
}
