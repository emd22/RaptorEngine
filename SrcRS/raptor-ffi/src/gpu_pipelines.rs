use std::ffi::{CStr, c_char, c_void};

use ash::vk::{self, Handle};
use raptor_gpu::{GraphicsPipelineDesc, PushConstantDef};
use raptor_shader::program::{self, MacroRef, ProgramKind};

use crate::gpu::RxGpuDevice;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxPushConstantDef
{
	pub size: u32,
	pub stages: u32,
}

/// # Safety
///
/// `device` must be live, `set_layouts` valid for `set_count` handles, `defs` for `def_count`
/// entries, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_pipeline_layout_create(
	device: *const RxGpuDevice,
	set_layouts: *const u64,
	set_count: usize,
	defs: *const RxPushConstantDef,
	def_count: usize,
	out: *mut u64,
) -> i32
{
	let sets: Vec<_> = if set_count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(set_layouts, set_count) }
			.iter()
			.map(|layout| vk::DescriptorSetLayout::from_raw(*layout))
			.collect()
	};

	let defs: Vec<_> = if def_count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(defs, def_count) }
			.iter()
			.map(|def| PushConstantDef {
				size: def.size,
				stages: vk::ShaderStageFlags::from_raw(def.stages),
			})
			.collect()
	};

	// SAFETY: guaranteed by the caller.
	match unsafe { &(*device).device }.create_pipeline_layout(&sets, &defs) {
		Ok(layout) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = layout.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `layout` one of its pipeline layouts that is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_pipeline_layout_destroy(device: *const RxGpuDevice, layout: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_pipeline_layout(vk::PipelineLayout::from_raw(layout))
	};
}

/// # Safety
///
/// `device` must be live, `code` valid for `word_count` words, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_shader_module_create(
	device: *const RxGpuDevice,
	code: *const u32,
	word_count: usize,
	out: *mut u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let code = unsafe { std::slice::from_raw_parts(code, word_count) };

	// SAFETY: guaranteed by the caller.
	match unsafe { &(*device).device }.create_shader_module(code) {
		Ok(module) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = module.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `module` one of its shader modules.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_shader_module_destroy(device: *const RxGpuDevice, module: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_shader_module(vk::ShaderModule::from_raw(module))
	};
}

#[repr(C)]
pub struct RxGraphicsPipelineDesc
{
	pub stage_flags: *const u32,
	pub stage_modules: *const u64,
	pub stage_count: usize,
	pub vertex_binding: *const c_void,
	pub vertex_attributes: *const c_void,
	pub vertex_attribute_count: usize,
	pub color_blend: *const c_void,
	pub color_blend_count: usize,
	pub attachment_formats: *const i32,
	pub attachment_count: usize,
	pub cull_mode: u32,
	pub front_face: i32,
	pub polygon_mode: i32,
	pub depth_compare_op: i32,
	pub render_lines: u8,
	pub disable_depth_test: u8,
	pub disable_depth_write: u8,
	pub layout: u64,
	pub render_pass: u64,
}

unsafe fn slice_or_empty<'a, T>(data: *const T, count: usize) -> &'a [T]
{
	if count == 0 || data.is_null() {
		&[]
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(data, count) }
	}
}

/// # Safety
///
/// `device` must be live, `desc` valid with every pointer valid for its count (`vertex_binding` may
/// be null), the referenced handles live, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_graphics_pipeline_create(
	device: *const RxGpuDevice,
	desc: *const RxGraphicsPipelineDesc,
	out: *mut u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (device, desc) = unsafe { (&(*device).device, &*desc) };

	// SAFETY: guaranteed by the caller.
	let (flags, modules, attributes, blends, formats) = unsafe {
		(
			slice_or_empty(desc.stage_flags, desc.stage_count),
			slice_or_empty(desc.stage_modules, desc.stage_count),
			slice_or_empty(
				desc.vertex_attributes
					.cast::<vk::VertexInputAttributeDescription>(),
				desc.vertex_attribute_count,
			),
			slice_or_empty(
				desc.color_blend
					.cast::<vk::PipelineColorBlendAttachmentState>(),
				desc.color_blend_count,
			),
			slice_or_empty(desc.attachment_formats, desc.attachment_count),
		)
	};

	let stages: Vec<_> = flags
		.iter()
		.zip(modules)
		.map(|(flags, module)| {
			(
				vk::ShaderStageFlags::from_raw(*flags),
				vk::ShaderModule::from_raw(*module),
			)
		})
		.collect();

	let formats: Vec<_> = formats
		.iter()
		.map(|format| vk::Format::from_raw(*format))
		.collect();

	let vertex_binding = if desc.vertex_binding.is_null() {
		None
	} else {
		// SAFETY: guaranteed by the caller.
		Some(unsafe {
			*desc
				.vertex_binding
				.cast::<vk::VertexInputBindingDescription>()
		})
	};

	let pipeline = device.create_graphics_pipeline(&GraphicsPipelineDesc {
		stages: &stages,
		vertex_binding,
		vertex_attributes: attributes,
		color_blend: blends,
		attachment_formats: &formats,
		cull_mode: vk::CullModeFlags::from_raw(desc.cull_mode),
		front_face: vk::FrontFace::from_raw(desc.front_face),
		polygon_mode: vk::PolygonMode::from_raw(desc.polygon_mode),
		depth_compare_op: vk::CompareOp::from_raw(desc.depth_compare_op),
		render_lines: desc.render_lines != 0,
		disable_depth_test: desc.disable_depth_test != 0,
		disable_depth_write: desc.disable_depth_write != 0,
		layout: vk::PipelineLayout::from_raw(desc.layout),
		render_pass: vk::RenderPass::from_raw(desc.render_pass),
	});

	match pipeline {
		Ok(pipeline) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = pipeline.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live, `module` and `layout` handles of it, and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_compute_pipeline_create(
	device: *const RxGpuDevice,
	module: u64,
	layout: u64,
	out: *mut u64,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &(*device).device };

	match device.create_compute_pipeline(
		vk::ShaderModule::from_raw(module),
		vk::PipelineLayout::from_raw(layout),
	) {
		Ok(pipeline) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = pipeline.as_raw() };
			vk::Result::SUCCESS.as_raw()
		}
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `device` must be live and `pipeline` one of its pipelines that is not in use.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_pipeline_destroy(device: *const RxGpuDevice, pipeline: u64)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device)
			.device
			.destroy_pipeline(vk::Pipeline::from_raw(pipeline))
	};
}

/// # Safety
///
/// `device` must be live, `cmd` recording, and `pipeline` live and usable with the bind point.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_bind_pipeline(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	bind_point: i32,
	pipeline: u64,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.cmd_bind_pipeline(
			vk::CommandBuffer::from_raw(cmd as u64),
			vk::PipelineBindPoint::from_raw(bind_point),
			vk::Pipeline::from_raw(pipeline),
		)
	};
}

/// # Safety
///
/// `device` must be live and `cmd` recording with a pipeline that has dynamic cull mode bound.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_cmd_set_cull_mode(
	device: *const RxGpuDevice,
	cmd: *mut c_void,
	mode: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		(*device).device.cmd_set_cull_mode(
			vk::CommandBuffer::from_raw(cmd as u64),
			vk::CullModeFlags::from_raw(mode),
		)
	};
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxShaderMacroRef
{
	pub name: *const c_char,
	pub value: *const c_char,
}

unsafe fn macro_refs<'a>(macros: *const RxShaderMacroRef, count: usize) -> Vec<MacroRef<'a>>
{
	// SAFETY: guaranteed by the caller.
	unsafe { slice_or_empty(macros, count) }
		.iter()
		.map(|entry| MacroRef {
			name: if entry.name.is_null() {
				None
			} else {
				// SAFETY: guaranteed by the caller.
				Some(unsafe { CStr::from_ptr(entry.name) }.to_bytes())
			},
			value: if entry.value.is_null() {
				None
			} else {
				// SAFETY: guaranteed by the caller.
				Some(unsafe { CStr::from_ptr(entry.value) }.to_bytes())
			},
		})
		.collect()
}

/// # Safety
///
/// `macros` must be valid for `count` entries whose strings are null or NUL-terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shader_hash_macros(
	macros: *const RxShaderMacroRef,
	count: usize,
	seed: u64,
) -> u64
{
	// SAFETY: guaranteed by the caller.
	program::hash_macros(&unsafe { macro_refs(macros, count) }, seed)
}

/// # Safety
///
/// As for `rx_shader_hash_macros`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shader_id(
	kind: u32,
	macros: *const RxShaderMacroRef,
	count: usize,
) -> u64
{
	let kind = match kind {
		1 => ProgramKind::Vertex,
		2 => ProgramKind::Pixel,
		4 => ProgramKind::Compute,
		_ => ProgramKind::Other,
	};

	// SAFETY: guaranteed by the caller.
	program::shader_id(kind, &unsafe { macro_refs(macros, count) })
}

/// # Safety
///
/// `words` must be valid for `count` words.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_spirv_input_location_mask(words: *const u32, count: usize) -> u32
{
	// SAFETY: guaranteed by the caller.
	program::input_location_mask(unsafe { slice_or_empty(words, count) })
}
