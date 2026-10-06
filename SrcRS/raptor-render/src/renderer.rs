use std::sync::Arc;

use ash::vk;
use raptor_core::{cvar, log_warn};
use raptor_entity::CameraCore;
use raptor_gfx::limits::*;
use raptor_gfx::{CommandBuffer, Gfx, GpuMarker, Image, gfx};
use raptor_gpu::{
	AddressMode, BlendAttachment, BufferRecord, Filter, ImageFormat, ImageRecord, SamplerProps,
};

use crate::descriptors::{DescriptorSet, Entry};
use crate::forward::{self, geometry_variant, light_grid};
use crate::gpu_ops::transition_record;
use crate::names::{Features, PipelineHandle, PipelineName, PipelinePass, ShaderName};
use crate::pipeline_desc::PipelineDesc;
use crate::pipelines::Pipelines;
use crate::push_constants::{
	CompositionPushConstants, DrawPushConstants, LightCullPushConstants, ShadowPushConstants,
	SsaoPushConstants,
};
use crate::shadow_atlas::{self, Region, ShadowAtlas};
use crate::shared::Shared;
use crate::stage::{RenderStage, StageTarget};

const VERTEX: vk::ShaderStageFlags = vk::ShaderStageFlags::VERTEX;
const PIXEL: vk::ShaderStageFlags = vk::ShaderStageFlags::FRAGMENT;
const COMPUTE: vk::ShaderStageFlags = vk::ShaderStageFlags::COMPUTE;
const VERTEX_PIXEL: vk::ShaderStageFlags =
	vk::ShaderStageFlags::from_raw(VERTEX.as_raw() | PIXEL.as_raw());

const STARTUP_FEATURES: [Features; 4] = [
	Features::NONE,
	Features::NORMAL_MAP,
	Features::SKINNED,
	Features(Features::SKINNED.0 | Features::NORMAL_MAP.0),
];

const DEFAULT_SSAO_RADIUS: f32 = 0.25;
const DEFAULT_SSAO_BIAS: f32 = 0.02;
const DEFAULT_SSAO_STRENGTH: f32 = 1.5;
const DEFAULT_SSAO_POWER: f32 = 1.2;
const DEFAULT_SSAO_FLOOR: f32 = 0.35;

pub const SHADOW_ATLAS_LAYOUT: vk::ImageLayout = vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL;

pub struct SceneInputs {
	pub object_buffer: Arc<BufferRecord>,
	pub object_bound_size: u64,
	pub object_page_size: u64,
	pub material_buffer: Arc<BufferRecord>,
	pub material_buffer_size: u64,
	pub null_rgba8: Arc<ImageRecord>,
	pub null_rg16: Arc<ImageRecord>,
	pub null_depth: Arc<ImageRecord>,
	pub declare_material: Box<dyn Fn(&mut PipelineDesc)>,
}

#[derive(Clone, Copy, Debug)]
pub struct LightCullOverride {
	pub light_count: u32,
	pub replaced_light: u32,
	pub replacement_slot: u32,
}

fn sampler(props: SamplerProps) -> vk::Sampler {
	gfx().sampler(&props)
}

fn default_sampler() -> vk::Sampler {
	sampler(SamplerProps::default())
}

fn linear_clamp() -> vk::Sampler {
	sampler(SamplerProps {
		address_mode: AddressMode::ClampToEdge,
		..SamplerProps::default()
	})
}

fn nearest() -> SamplerProps {
	SamplerProps {
		min_filter: Filter::Nearest,
		mag_filter: Filter::Nearest,
		mip_filter: Filter::Nearest,
		..SamplerProps::default()
	}
}

fn region_rect(region: Region) -> vk::Rect2D {
	vk::Rect2D {
		offset: vk::Offset2D {
			x: region.offset_x as i32,
			y: region.offset_y as i32,
		},
		extent: vk::Extent2D {
			width: region.width,
			height: region.height,
		},
	}
}

pub struct ShadowAtlasGpu {
	pub stage: RenderStage,
	pub state: ShadowAtlas,
}

impl ShadowAtlasGpu {
	pub fn new(gfx: &Gfx) -> Self {
		let size = (shadow_atlas::WIDTH, shadow_atlas::HEIGHT);

		let stage = RenderStage::new(gfx, "ShadowAtlas", Some(size), 1);

		let mut target = StageTarget::new(
			ImageFormat::D32Float,
			vk::ImageUsageFlags::SAMPLED | vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT,
			vk::ImageAspectFlags::DEPTH,
		);

		target.size = Some(size);
		target.load_op = vk::AttachmentLoadOp::CLEAR;
		target.initial_layout = SHADOW_ATLAS_LAYOUT;
		target.final_layout = SHADOW_ATLAS_LAYOUT;
		target.stencil_load_op = vk::AttachmentLoadOp::DONT_CARE;
		target.stencil_store_op = vk::AttachmentStoreOp::DONT_CARE;

		stage.add_target(target);
		stage.build(gfx);

		Self {
			stage,
			state: ShadowAtlas::default(),
		}
	}

	pub fn target(&self) -> Arc<ImageRecord> {
		self.stage
			.target_image(ImageFormat::D32Float)
			.expect("the shadow atlas has a depth target")
	}

	pub fn uv_transform(&self, region: Region) -> [f32; 4] {
		shadow_atlas::region_uv_transform(region)
	}

	pub fn make_desc(
		&self,
		inputs: &Arc<Shared<SceneInputs>>,
		features: Features,
		desc: &mut PipelineDesc,
	) -> bool {
		make_shadow_desc(self.stage.record_ptr(), inputs, features, desc)
	}

	pub fn begin_region(&mut self, gfx: &Gfx, pipelines: &Pipelines, region: Region) {
		let cmd = gfx.frame_cmd();

		let image = self.target();

		if image.fields().layout != SHADOW_ATLAS_LAYOUT.as_raw() {
			transition_record(gfx, cmd, &image, SHADOW_ATLAS_LAYOUT, 0, 1);
		}

		let render_area = self.state.begin_region(region);

		self.stage.begin_area(
			gfx,
			cmd.raw(),
			region_rect(render_area),
			region_rect(region),
		);

		let handle = pipelines.get_or_create_variant(gfx, PipelinePass::Shadow, Features::NONE);

		self.bind_pipeline(gfx, pipelines, handle);
	}

	pub fn end_region(&self, gfx: &Gfx) {
		self.stage.end(gfx, gfx.frame_cmd().raw());
	}

	pub fn bind_pipeline(&self, gfx: &Gfx, pipelines: &Pipelines, handle: PipelineHandle) {
		let cmd = gfx.frame_cmd();

		if let Some(slot) = pipelines.slot(gfx, handle)
			&& slot.is_built()
		{
			pipelines.bind_pipeline(gfx, cmd, slot);
		}
	}
}

fn make_shadow_desc(
	stage: *mut raptor_gpu::RenderStageRecord,
	inputs: &Arc<Shared<SceneInputs>>,
	features: Features,
	desc: &mut PipelineDesc,
) -> bool {
	if features != Features::NONE
		&& features != Features::ALPHA_MASK
		&& features != Features::SKINNED
	{
		return false;
	}

	let is_masked = features == Features::ALPHA_MASK;
	let is_skinned = features == Features::SKINNED;

	desc.features = features.0;
	desc.debug_name = if is_masked {
		"ShadowDirectionalMasked"
	} else if is_skinned {
		"ShadowDirectionalSkinned"
	} else {
		"ShadowDirectional"
	}
	.to_owned();

	desc.set_shader(ShaderName::Shadows as u32, ShaderName::Shadows.name());

	if is_masked {
		desc.set_macros(&[("ALPHA_MASK", "1")]);
	} else if is_skinned {
		desc.set_macros(&[("USE_SKINNING", "1")]);
	}

	desc.stage = stage;
	desc.vertex_type = if is_skinned {
		raptor_gpu::VertexType::Skinned as u32
	} else {
		raptor_gpu::VertexType::Default as u32
	};
	desc.depth_compare_op = vk::CompareOp::GREATER.as_raw();
	desc.cull_mode = vk::CullModeFlags::BACK.as_raw();
	desc.front_face = vk::FrontFace::CLOCKWISE.as_raw();

	desc.add_push_constants::<ShadowPushConstants>(VERTEX);

	let inputs = Shared(Arc::clone(inputs));

	desc.declare = Some(Box::new(move |desc| {
		desc.add_buffer(
			0,
			0,
			VERTEX,
			&inputs.object_buffer,
			0,
			inputs.object_page_size,
		);
		desc.add_buffer(
			0,
			1,
			PIXEL,
			&inputs.material_buffer,
			0,
			inputs.material_buffer_size,
		);

		if is_masked || is_skinned {
			(inputs.declare_material)(desc);
		}
	}));

	true
}

fn blend_over() -> BlendAttachment {
	BlendAttachment {
		enabled: true,
		color_op: vk::BlendOp::ADD,
		alpha_op: vk::BlendOp::ADD,
		src_color: vk::BlendFactor::SRC_ALPHA,
		dst_color: vk::BlendFactor::ONE_MINUS_SRC_ALPHA,
		src_alpha: vk::BlendFactor::ONE,
		dst_alpha: vk::BlendFactor::ONE_MINUS_SRC_ALPHA,
		..BlendAttachment::default()
	}
}

struct ForwardTemplate {
	forward_stage: Shared<*mut raptor_gpu::RenderStageRecord>,
	forward_color_index: u32,
	inputs: Arc<Shared<SceneInputs>>,
	ssao_blur: Shared<Arc<ImageRecord>>,
}

fn add_decal_descriptors(desc: &mut PipelineDesc, inputs: &SceneInputs) {
	let gfx = gfx();
	let buffers = gfx.buffers();

	desc.add_buffer(
		0,
		9,
		PIXEL,
		buffers.decal.record(),
		0,
		u64::from(buffers.decal_page_size),
	);
	desc.add_buffer(
		0,
		10,
		PIXEL,
		buffers.decal_mask.record(),
		0,
		u64::from(buffers.decal_mask_page_size),
	);
	desc.add_image(0, 11, PIXEL, &inputs.null_rgba8, default_sampler());
	desc.add_image(0, 12, PIXEL, &inputs.null_rgba8, default_sampler());
	desc.add_image(0, 17, PIXEL, &inputs.null_rgba8, default_sampler());
}

fn add_global_descriptors(desc: &mut PipelineDesc, inputs: &SceneInputs, ssao: &Arc<ImageRecord>) {
	let gfx = gfx();
	let buffers = gfx.buffers();

	desc.add_buffer(
		0,
		0,
		VERTEX,
		&inputs.object_buffer,
		0,
		inputs.object_page_size,
	);
	desc.add_buffer(
		0,
		1,
		PIXEL,
		&inputs.material_buffer,
		0,
		inputs.material_buffer_size,
	);
	desc.add_buffer(
		0,
		2,
		PIXEL,
		buffers.light_grid.record(),
		0,
		u64::from(buffers.light_grid_page_size),
	);
	desc.add_buffer(
		0,
		3,
		PIXEL,
		buffers.light_index_list.record(),
		0,
		u64::from(buffers.light_index_list_page_size),
	);
	desc.add_buffer(
		0,
		6,
		PIXEL,
		buffers.probe.record(),
		0,
		u64::from(buffers.probe_page_size),
	);
	desc.add_buffer(
		0,
		7,
		PIXEL,
		buffers.probe_volume.record(),
		0,
		u64::from(buffers.probe_volume_page_size),
	);
	desc.add_buffer(
		0,
		13,
		PIXEL,
		buffers.probe_grid.record(),
		0,
		u64::from(buffers.probe_grid_page_size),
	);
	desc.add_image(0, 8, PIXEL, &inputs.null_rg16, default_sampler());
	desc.add_buffer(
		0,
		14,
		PIXEL,
		buffers.reflection_probe.record(),
		0,
		u64::from(buffers.reflection_probe_page_size),
	);
	desc.add_image(0, 15, PIXEL, &inputs.null_rg16, default_sampler());
	desc.add_image(0, 16, PIXEL, &inputs.null_rg16, default_sampler());

	add_decal_descriptors(desc, inputs);

	desc.add_image(0, 4, PIXEL, &inputs.null_depth, default_sampler());
	desc.add_image(0, 5, PIXEL, ssao, sampler(SamplerProps::default()));
}

fn make_forward_desc(
	template: &ForwardTemplate,
	pass: PipelinePass,
	features: Features,
	desc: &mut PipelineDesc,
) -> bool {
	let is_opaque = matches!(pass, PipelinePass::Forward | PipelinePass::ForwardDebug);
	let is_blended = matches!(
		pass,
		PipelinePass::ForwardBlend | PipelinePass::ForwardBlendDebug
	);
	let is_debug = matches!(
		pass,
		PipelinePass::ForwardDebug | PipelinePass::ForwardBlendDebug
	);

	if features.0 & Features::ALPHA_MASK.0 != 0 {
		return false;
	}

	let variant = geometry_variant(features.0);

	desc.features = variant.features;

	let mut macros: Vec<(&str, &str)> = variant.macros.iter().map(|name| (*name, "1")).collect();

	if is_blended {
		macros.push(("ALPHA_CUTOFF", "0.01"));
	}

	let pass_suffix;

	if is_opaque {
		desc.depth_compare_op = vk::CompareOp::EQUAL.as_raw();
		desc.depth_write = false;
		macros.push(("USE_PREPASS_DEPTH", "1"));
		pass_suffix = "";
	} else if is_blended {
		pass_suffix = "Transparent";
	} else {
		pass_suffix = "Capture";
		macros.push(("PROBE_CAPTURE", "1"));
	}

	if is_debug {
		macros.push(("DEBUG_VIEWS", "1"));
	}

	desc.debug_name = format!(
		"Geometry{}{pass_suffix}{}",
		variant.suffix,
		if is_debug { "Debug" } else { "" }
	);

	desc.set_shader(ShaderName::Forward as u32, ShaderName::Forward.name());
	desc.set_macros(&macros);
	desc.vertex_type = variant.vertex_type;
	desc.stage = *template.forward_stage;
	desc.cull_mode = vk::CullModeFlags::BACK.as_raw();

	desc.add_push_constants::<DrawPushConstants>(VERTEX_PIXEL);

	if is_blended {
		desc.depth_write = false;

		desc.add_blend(template.forward_color_index, blend_over());
	}

	let inputs = Shared(Arc::clone(&template.inputs));
	let ssao = Shared(Arc::clone(&template.ssao_blur));

	desc.declare = Some(Box::new(move |desc| {
		add_global_descriptors(desc, &inputs, &ssao);
		(inputs.declare_material)(desc);
	}));

	true
}

fn make_prepass_desc(
	stage: *mut raptor_gpu::RenderStageRecord,
	inputs: &Arc<Shared<SceneInputs>>,
	features: Features,
	desc: &mut PipelineDesc,
) -> bool {
	if features.0 & Features::ALPHA_MASK.0 != 0 {
		return false;
	}

	let variant = geometry_variant(features.0);

	desc.features = variant.features;
	desc.debug_name = format!("DepthNormal{}", variant.suffix);

	let macros: Vec<(&str, &str)> = variant.macros.iter().map(|name| (*name, "1")).collect();

	desc.set_shader(
		ShaderName::DepthNormal as u32,
		ShaderName::DepthNormal.name(),
	);
	desc.set_macros(&macros);
	desc.vertex_type = variant.vertex_type;
	desc.stage = stage;
	desc.cull_mode = vk::CullModeFlags::BACK.as_raw();

	desc.add_push_constants::<DrawPushConstants>(VERTEX_PIXEL);

	let inputs = Shared(Arc::clone(inputs));

	desc.declare = Some(Box::new(move |desc| {
		desc.add_buffer(
			0,
			0,
			VERTEX,
			&inputs.object_buffer,
			0,
			inputs.object_page_size,
		);
		desc.add_buffer(
			0,
			1,
			PIXEL,
			&inputs.material_buffer,
			0,
			inputs.material_buffer_size,
		);

		(inputs.declare_material)(desc);
	}));

	true
}

pub struct Renderer {
	pub prepass: RenderStage,
	pub forward_pass: RenderStage,
	pub ssao_pass: RenderStage,
	pub ssao_blur_pass: RenderStage,
	pub comp_pass: RenderStage,
	pub persistent: Option<DescriptorSet>,
	pub persistent_slim: Option<DescriptorSet>,
	pub light_tile_columns: u32,
	pub light_tile_rows: u32,
	decal_atlas: Option<Arc<ImageRecord>>,
	decal_normal_atlas: Option<Arc<ImageRecord>>,
	decal_blood_atlas: Option<Arc<ImageRecord>>,
	inputs: Arc<Shared<SceneInputs>>,
	text_instance_buffer: Arc<BufferRecord>,
	pipelines: Arc<Pipelines>,
}

fn color_stage(gfx: &Gfx, name: &'static str, divisor: u32, format: ImageFormat) -> RenderStage {
	let stage = RenderStage::new(gfx, name, None, divisor);

	stage.add_target(StageTarget::color(format));
	stage.build(gfx);

	stage
}

impl Renderer {
	pub fn new(
		gfx: &Gfx,
		pipelines: Arc<Pipelines>,
		inputs: SceneInputs,
		shadow: &ShadowAtlasGpu,
		text_instance_buffer: Arc<BufferRecord>,
	) -> Self {
		let text_atlas_placeholder = Arc::clone(&inputs.null_rgba8);
		let inputs = Arc::new(Shared(inputs));

		let prepass = RenderStage::new(gfx, "DepthNormal", None, 1);
		prepass.add_target(StageTarget::depth(ImageFormat::D32Float));
		prepass.add_target(StageTarget::color(ImageFormat::Rgba16Float));
		prepass.build(gfx);

		let ssao_pass = color_stage(gfx, "SSAO", 2, ImageFormat::R8UNorm);
		let ssao_blur_pass = color_stage(gfx, "SSAOBlur", 2, ImageFormat::R8UNorm);

		let prepass_depth = prepass
			.target_image(ImageFormat::D32Float)
			.expect("the prepass has a depth target");

		let forward_pass = RenderStage::new(gfx, "Forward", None, 1);
		forward_pass.add_target(StageTarget::color(ImageFormat::Rgba16Float));

		let mut depth = StageTarget::depth(ImageFormat::D32Float);
		depth.reference = Some(prepass_depth);
		depth.load_op = vk::AttachmentLoadOp::LOAD;
		depth.stencil_load_op = vk::AttachmentLoadOp::DONT_CARE;
		depth.stencil_store_op = vk::AttachmentStoreOp::DONT_CARE;
		depth.initial_layout = vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
		forward_pass.add_target(depth);
		forward_pass.build(gfx);

		let comp_pass = RenderStage::new(gfx, "Compose", None, 1);
		comp_pass.mark_final();
		comp_pass.build(gfx);

		let mut renderer = Self {
			prepass,
			forward_pass,
			ssao_pass,
			ssao_blur_pass,
			comp_pass,
			persistent: None,
			persistent_slim: None,
			light_tile_columns: 0,
			light_tile_rows: 0,
			decal_atlas: None,
			decal_normal_atlas: None,
			decal_blood_atlas: None,
			inputs,
			text_instance_buffer,
			pipelines,
		};

		renderer.create_pipelines(gfx, shadow, &text_atlas_placeholder);
		renderer.build_persistent_descriptors(gfx, shadow);

		renderer
	}

	pub fn pipelines(&self) -> &Arc<Pipelines> {
		&self.pipelines
	}

	fn create_pipelines(
		&mut self,
		gfx: &Gfx,
		shadow: &ShadowAtlasGpu,
		text_atlas: &Arc<ImageRecord>,
	) {
		self.create_depth_normal_pipelines(gfx);
		self.create_ssao_pipelines(gfx);
		self.create_forward_pipelines(gfx);
		self.create_composition_pipelines(gfx);
		self.create_text_pipelines(gfx, text_atlas);
		self.create_light_culling_pipeline(gfx);
		self.create_debug_pipelines(gfx);
		self.register_shadow_pipelines(gfx, shadow);
	}

	fn register_shadow_pipelines(&self, gfx: &Gfx, shadow: &ShadowAtlasGpu) {
		let stage = Shared(shadow.stage.record_ptr());
		let inputs = Shared(Arc::clone(&self.inputs));

		self.pipelines.register_template(
			PipelinePass::Shadow,
			Arc::new(move |features, desc| {
				make_shadow_desc(*stage, &inputs.0, Features(features), desc)
			}),
		);

		for features in [Features::NONE, Features::ALPHA_MASK, Features::SKINNED] {
			self.pipelines
				.get_or_create_variant(gfx, PipelinePass::Shadow, features);
		}
	}

	fn create_depth_normal_pipelines(&self, gfx: &Gfx) {
		let stage = Shared(self.prepass.record_ptr());
		let inputs = Shared(Arc::clone(&self.inputs));

		self.pipelines.register_template(
			PipelinePass::Depth,
			Arc::new(move |features, desc| {
				make_prepass_desc(*stage, &inputs.0, Features(features), desc)
			}),
		);

		for features in STARTUP_FEATURES {
			self.pipelines
				.get_or_create_variant(gfx, PipelinePass::Depth, features);
		}
	}

	fn create_forward_pipelines(&self, gfx: &Gfx) {
		let template = Arc::new(Shared(ForwardTemplate {
			forward_stage: Shared(self.forward_pass.record_ptr()),
			forward_color_index: self
				.forward_pass
				.target_index(ImageFormat::Rgba16Float, 0)
				.expect("the forward pass has a color target") as u32,
			inputs: Arc::clone(&self.inputs),
			ssao_blur: Shared(
				self.ssao_blur_pass
					.target_image(ImageFormat::R8UNorm)
					.expect("the blur pass has a target"),
			),
		}));

		for pass in [
			PipelinePass::Forward,
			PipelinePass::ForwardBlend,
			PipelinePass::ForwardCapture,
			PipelinePass::ForwardDebug,
			PipelinePass::ForwardBlendDebug,
		] {
			let template = Arc::clone(&template);

			self.pipelines.register_template(
				pass,
				Arc::new(move |features, desc| {
					make_forward_desc(&template.0, pass, Features(features), desc)
				}),
			);
		}

		for pass in [
			PipelinePass::Forward,
			PipelinePass::ForwardBlend,
			PipelinePass::ForwardCapture,
		] {
			for features in STARTUP_FEATURES {
				self.pipelines.get_or_create_variant(gfx, pass, features);
			}
		}
	}

	fn create_ssao_pipelines(&self, gfx: &Gfx) {
		let prepass_depth = self
			.prepass
			.target_image(ImageFormat::D32Float)
			.expect("the prepass has a depth target");
		let prepass_normal = self
			.prepass
			.target_image(ImageFormat::Rgba16Float)
			.expect("the prepass has a normal target");

		let mut desc = PipelineDesc::default();

		desc.use_stage(&self.ssao_pass);
		desc.set_shader(ShaderName::Ssao as u32, ShaderName::Ssao.name());
		desc.no_vertices = true;
		desc.cull_mode = 0;
		desc.add_push_constants::<SsaoPushConstants>(PIXEL);
		desc.add_image(0, 0, PIXEL, &prepass_depth, sampler(nearest()));
		desc.add_image(0, 1, PIXEL, &prepass_normal, default_sampler());

		let noise = gfx
			.noise_texture()
			.expect("the noise texture is made before the renderer");

		desc.add_image(0, 2, PIXEL, noise.record(), sampler(nearest()));

		self.pipelines.build_static(gfx, PipelineName::Ssao, desc);

		let ssao = self
			.ssao_pass
			.target_image(ImageFormat::R8UNorm)
			.expect("the ssao pass has a target");

		let mut desc = PipelineDesc::default();

		desc.use_stage(&self.ssao_blur_pass);
		desc.set_shader(ShaderName::SsaoBlur as u32, ShaderName::SsaoBlur.name());
		desc.no_vertices = true;
		desc.cull_mode = 0;
		desc.add_push_constants::<forward::SsaoBlurPush>(PIXEL);
		desc.add_image(0, 0, PIXEL, &ssao, sampler(nearest()));
		desc.add_image(0, 1, PIXEL, &prepass_depth, sampler(nearest()));

		self.pipelines
			.build_static(gfx, PipelineName::SsaoBlur, desc);
	}

	fn create_debug_pipelines(&self, gfx: &Gfx) {
		for (name, lines) in [
			(PipelineName::DebugLayer, true),
			(PipelineName::DebugSolid, false),
		] {
			let mut desc = PipelineDesc::default();

			desc.add_push_constants::<crate::push_constants::DebugLayerPushConstants>(VERTEX);
			desc.use_stage(&self.forward_pass);
			desc.set_shader(ShaderName::DebugLayer as u32, ShaderName::DebugLayer.name());
			desc.vertex_type = raptor_gpu::VertexType::Slim as u32;
			desc.render_lines = lines;
			desc.cull_mode = vk::CullModeFlags::BACK.as_raw();

			self.pipelines.build_static(gfx, name, desc);
		}
	}

	fn create_composition_pipelines(&self, gfx: &Gfx) {
		let lighting = self
			.forward_pass
			.target_image(ImageFormat::Rgba16Float)
			.expect("the forward pass has a color target");

		for (name, macros) in [
			(PipelineName::Composition, &[][..]),
			(PipelineName::CompositionAgx, &[("USE_AGX", "1")][..]),
		] {
			let mut desc = PipelineDesc::default();

			desc.use_stage(&self.comp_pass);
			desc.set_shader(
				ShaderName::Composition as u32,
				ShaderName::Composition.name(),
			);
			desc.set_macros(macros);
			desc.no_vertices = true;
			desc.cull_mode = 0;
			desc.depth_test = false;
			desc.depth_write = false;
			desc.add_push_constants::<CompositionPushConstants>(PIXEL);
			desc.add_image(0, 2, PIXEL, &lighting, default_sampler());

			self.pipelines.build_static(gfx, name, desc);
		}
	}

	fn create_text_pipelines(&self, gfx: &Gfx, atlas: &Arc<ImageRecord>) {
		let color_index = self
			.forward_pass
			.target_index(ImageFormat::Rgba16Float, 0)
			.expect("the forward pass has a color target") as u32;

		for (name, macros) in [
			(PipelineName::TextRendering, &[][..]),
			(PipelineName::ImageRendering, &[("DRAW_IMAGE", "1")][..]),
		] {
			let mut desc = PipelineDesc::default();

			desc.add_push_constants::<crate::push_constants::TextPushConstants>(VERTEX);
			desc.use_stage(&self.forward_pass);
			desc.set_shader(ShaderName::BitmapText as u32, ShaderName::BitmapText.name());
			desc.set_macros(macros);
			desc.vertex_type = raptor_gpu::VertexType::Default as u32;
			desc.cull_mode = 0;
			desc.depth_test = false;
			desc.depth_write = false;
			desc.add_blend(color_index, blend_over());
			desc.add_buffer(
				0,
				0,
				VERTEX,
				&self.text_instance_buffer,
				0,
				u64::from(crate::text::INSTANCE_SIZE) * crate::text::MAX_GLYPHS as u64,
			);
			desc.add_image(0, 1, PIXEL, atlas, sampler(nearest()));

			self.pipelines.build_static(gfx, name, desc);
		}
	}

	fn create_light_culling_pipeline(&self, gfx: &Gfx) {
		let buffers = gfx.buffers();
		let light = buffers
			.light
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		let mut desc = PipelineDesc::default();

		desc.add_push_constants::<LightCullPushConstants>(COMPUTE);
		desc.set_shader(
			ShaderName::LightCulling as u32,
			ShaderName::LightCulling.name(),
		);
		desc.cull_mode = 0;
		desc.add_buffer(
			0,
			0,
			COMPUTE,
			buffers.light_grid.record(),
			0,
			u64::from(buffers.light_grid_page_size),
		);
		desc.add_buffer(
			0,
			1,
			COMPUTE,
			buffers.light_index_list.record(),
			0,
			u64::from(buffers.light_index_list_page_size),
		);
		desc.add_buffer(
			0,
			4,
			COMPUTE,
			light.gpu_buffer().record(),
			0,
			u64::from(light.page_size()),
		);
		desc.add_buffer(
			0,
			5,
			COMPUTE,
			buffers.decal.record(),
			0,
			u64::from(buffers.decal_page_size),
		);
		desc.add_buffer(
			0,
			6,
			COMPUTE,
			buffers.decal_mask.record(),
			0,
			u64::from(buffers.decal_mask_page_size),
		);

		drop(light);

		self.pipelines
			.build_static(gfx, PipelineName::LightCulling, desc);
	}

	fn build_persistent_descriptors(&mut self, gfx: &Gfx, shadow: &ShadowAtlasGpu) {
		let buffers = gfx.buffers();
		let inputs = &self.inputs;

		let slim = [
			Entry::Buffer {
				binding: 0,
				stages: VERTEX,
				buffer: &inputs.object_buffer,
				offset: 0,
				range: inputs.object_bound_size,
			},
			Entry::Buffer {
				binding: 1,
				stages: PIXEL,
				buffer: &inputs.material_buffer,
				offset: 0,
				range: inputs.material_buffer_size,
			},
		];

		self.persistent_slim = Some(DescriptorSet::request(gfx, &slim));

		let dfg = gfx
			.dfg_lut()
			.expect("the DFG LUT is made before the renderer");

		let ssao = self
			.ssao_blur_pass
			.target_image(ImageFormat::R8UNorm)
			.expect("the blur pass has a target");

		let shadow_target = shadow.target();

		let moments_sampler = sampler(SamplerProps {
			address_mode: AddressMode::ClampToEdge,
			..SamplerProps::default()
		});

		let reflection_sampler = sampler(SamplerProps {
			address_mode: AddressMode::ClampToEdge,
			max_lod: (REFLECTION_PROBE_MIPS - 1) as f32,
			..SamplerProps::default()
		});

		let shadow_sampler = sampler(SamplerProps {
			address_mode: AddressMode::ClampToBorder,
			border_color: raptor_gpu::BorderColor::FloatWhite,
			compare_op: raptor_gpu::CompareOp::Greater,
			..SamplerProps::default()
		});

		let decal_atlas = self.decal_atlas.as_ref().unwrap_or(&inputs.null_rgba8);
		let decal_normal = self
			.decal_normal_atlas
			.as_ref()
			.unwrap_or(&inputs.null_rgba8);
		let decal_blood = self
			.decal_blood_atlas
			.as_ref()
			.unwrap_or(&inputs.null_rgba8);

		let full = [
			Entry::Buffer {
				binding: 0,
				stages: VERTEX,
				buffer: &inputs.object_buffer,
				offset: 0,
				range: inputs.object_bound_size,
			},
			Entry::Buffer {
				binding: 1,
				stages: PIXEL,
				buffer: &inputs.material_buffer,
				offset: 0,
				range: inputs.material_buffer_size,
			},
			Entry::Buffer {
				binding: 2,
				stages: PIXEL,
				buffer: buffers.light_grid.record(),
				offset: 0,
				range: u64::from(buffers.light_grid_page_size),
			},
			Entry::Buffer {
				binding: 3,
				stages: PIXEL,
				buffer: buffers.light_index_list.record(),
				offset: 0,
				range: u64::from(buffers.light_index_list_page_size),
			},
			Entry::Buffer {
				binding: 6,
				stages: PIXEL,
				buffer: buffers.probe.record(),
				offset: 0,
				range: u64::from(buffers.probe_page_size),
			},
			Entry::Buffer {
				binding: 7,
				stages: PIXEL,
				buffer: buffers.probe_volume.record(),
				offset: 0,
				range: u64::from(buffers.probe_volume_page_size),
			},
			Entry::Buffer {
				binding: 13,
				stages: PIXEL,
				buffer: buffers.probe_grid.record(),
				offset: 0,
				range: u64::from(buffers.probe_grid_page_size),
			},
			Entry::Image {
				binding: 8,
				stages: PIXEL,
				image: buffers.probe_moments_atlas.record(),
				sampler: moments_sampler,
			},
			Entry::Buffer {
				binding: 14,
				stages: PIXEL,
				buffer: buffers.reflection_probe.record(),
				offset: 0,
				range: u64::from(buffers.reflection_probe_page_size),
			},
			Entry::Image {
				binding: 15,
				stages: PIXEL,
				image: buffers.reflection_probes.record(),
				sampler: reflection_sampler,
			},
			Entry::Image {
				binding: 16,
				stages: PIXEL,
				image: dfg.record(),
				sampler: linear_clamp(),
			},
			Entry::Image {
				binding: 4,
				stages: PIXEL,
				image: &shadow_target,
				sampler: shadow_sampler,
			},
			Entry::Image {
				binding: 5,
				stages: PIXEL,
				image: &ssao,
				sampler: default_sampler(),
			},
			Entry::Buffer {
				binding: 9,
				stages: PIXEL,
				buffer: buffers.decal.record(),
				offset: 0,
				range: u64::from(buffers.decal_page_size),
			},
			Entry::Buffer {
				binding: 10,
				stages: PIXEL,
				buffer: buffers.decal_mask.record(),
				offset: 0,
				range: u64::from(buffers.decal_mask_page_size),
			},
			Entry::Image {
				binding: 11,
				stages: PIXEL,
				image: decal_atlas,
				sampler: default_sampler(),
			},
			Entry::Image {
				binding: 12,
				stages: PIXEL,
				image: decal_normal,
				sampler: default_sampler(),
			},
			Entry::Image {
				binding: 17,
				stages: PIXEL,
				image: decal_blood,
				sampler: default_sampler(),
			},
		];

		self.persistent = Some(DescriptorSet::request(gfx, &full));
	}

	pub fn set_decal_atlases(
		&mut self,
		gfx: &Gfx,
		shadow: &ShadowAtlasGpu,
		atlas: Option<&Image>,
		normal_atlas: Option<&Image>,
		blood_atlas: Option<&Image>,
	) {
		let record = |image: Option<&Image>| image.map(|image| Arc::clone(image.record()));

		let (atlas, normal_atlas, blood_atlas) =
			(record(atlas), record(normal_atlas), record(blood_atlas));

		let same = |a: &Option<Arc<ImageRecord>>, b: &Option<Arc<ImageRecord>>| match (a, b) {
			(None, None) => true,
			(Some(a), Some(b)) => Arc::ptr_eq(a, b),
			_ => false,
		};

		if same(&atlas, &self.decal_atlas)
			&& same(&normal_atlas, &self.decal_normal_atlas)
			&& same(&blood_atlas, &self.decal_blood_atlas)
		{
			return;
		}

		self.decal_atlas = atlas;
		self.decal_normal_atlas = normal_atlas;
		self.decal_blood_atlas = blood_atlas;

		self.build_persistent_descriptors(gfx, shadow);
	}

	pub fn forward_buffer_offsets(&self, gfx: &Gfx, object_base_offset: u32) -> [u32; 10] {
		let buffers = gfx.buffers();
		let frame = gfx.frame_number();

		[
			object_base_offset,
			0,
			buffers.light_grid_frame_offset(frame),
			buffers.light_index_list_frame_offset(frame),
			0,
			0,
			buffers.decal_frame_offset(frame),
			buffers.decal_mask_frame_offset(frame),
			0,
			0,
		]
	}

	pub fn bind_persistent(
		&self,
		gfx: &Gfx,
		cmd: &CommandBuffer,
		slot: &crate::pipeline_cache::PipelineSlot,
		object_base_offset: u32,
	) {
		let offsets = self.forward_buffer_offsets(gfx, object_base_offset);

		if let Some(set) = &self.persistent {
			set.bind(gfx, cmd, slot, 0, &offsets);
		}
	}

	pub fn bind_persistent_slim(
		&self,
		gfx: &Gfx,
		cmd: &CommandBuffer,
		slot: &crate::pipeline_cache::PipelineSlot,
		object_base_offset: u32,
	) {
		if let Some(set) = &self.persistent_slim {
			set.bind(gfx, cmd, slot, 0, &[object_base_offset, 0]);
		}
	}

	pub fn stages(&self) -> [RenderStage; 5] {
		[
			self.prepass.clone(),
			self.forward_pass.clone(),
			self.ssao_pass.clone(),
			self.ssao_blur_pass.clone(),
			self.comp_pass.clone(),
		]
	}

	pub fn release(&self, gfx: &Gfx) {
		for stage in self.stages() {
			stage.release(gfx);
		}
	}

	pub fn begin_prepass(&self, gfx: &Gfx) {
		self.prepass.begin(gfx, gfx.frame_cmd().raw());
	}

	pub fn begin_geometry(&self, gfx: &Gfx) {
		self.forward_pass.begin(gfx, gfx.frame_cmd().raw());
	}

	pub fn rebuild_stages(&self, gfx: &Gfx) {
		gfx.wait_idle();

		self.prepass.rebuild(gfx, None);
		self.forward_pass.rebuild(gfx, None);
		self.ssao_pass.rebuild(gfx, None);
		self.ssao_blur_pass.rebuild(gfx, None);
		self.comp_pass.rebuild(gfx, None);
	}

	pub fn do_light_culling_pass(
		&mut self,
		gfx: &Gfx,
		camera: &CameraCore,
		extent_override: Option<(u32, u32)>,
		light_override: Option<LightCullOverride>,
		decal_count: u32,
	) {
		let cmd = gfx.frame_cmd();
		let buffers = gfx.buffers();
		let frame = gfx.frame_number();

		let extent = extent_override.unwrap_or_else(|| gfx.swapchain().extent());

		let grid = light_grid(
			[extent.0, extent.1],
			LIGHT_TILE_SIZE,
			MAX_SCREEN_TILES_X,
			MAX_SCREEN_TILES_Y,
		);

		if grid.capped {
			log_warn!(
				Render;
				"Light grid capped at {}x{} tiles for a {}x{} view, raise MAX_SCREEN_TILES",
				grid.columns,
				grid.rows,
				extent.0,
				extent.1
			);
		}

		self.light_tile_columns = grid.columns;
		self.light_tile_rows = grid.rows;

		let (light_slot_count, light_base_offset) = {
			let light = buffers
				.light
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner());

			(light.slot_index(), light.base_offset(frame))
		};

		let mut push = LightCullPushConstants {
			camera_matrix: camera.camera_matrix,
			screen_size: [extent.0 as f32, extent.1 as f32],
			light_count: light_slot_count,
			tile_columns: grid.columns,
			..LightCullPushConstants::default()
		};

		if let Some(overrides) = light_override {
			push.light_count = overrides.light_count;
			push.replaced_light = overrides.replaced_light;
			push.replacement_slot = overrides.replacement_slot;
		}

		let is_main_view = extent_override.is_none() && light_override.is_none();

		push.decal_count = if is_main_view { decal_count } else { 0 };

		if !is_main_view {
			raptor_gfx::barrier::buffer_fragment_to_compute(
				gfx.device(),
				cmd.raw(),
				&buffers.light_grid,
			);
			raptor_gfx::barrier::buffer_fragment_to_compute(
				gfx.device(),
				cmd.raw(),
				&buffers.light_index_list,
			);
			raptor_gfx::barrier::buffer_fragment_to_compute(
				gfx.device(),
				cmd.raw(),
				&buffers.decal_mask,
			);
		}

		self.pipelines
			.add_buffer_offset(0, buffers.light_grid_frame_offset(frame));
		self.pipelines
			.add_buffer_offset(0, buffers.light_index_list_frame_offset(frame));
		self.pipelines.add_buffer_offset(0, light_base_offset);
		self.pipelines
			.add_buffer_offset(0, buffers.decal_frame_offset(frame));
		self.pipelines
			.add_buffer_offset(0, buffers.decal_mask_frame_offset(frame));

		self.pipelines
			.bind_named(gfx, cmd, PipelineName::LightCulling);

		if let Some(slot) = self
			.pipelines
			.slot(gfx, PipelineName::LightCulling.handle())
		{
			self.pipelines
				.push_constants(gfx, cmd, slot, COMPUTE, bytemuck::bytes_of(&push));
		}

		// SAFETY: the command buffer is recording outside a render pass with the compute pipeline
		// bound.
		unsafe {
			gfx.device()
				.raw()
				.cmd_dispatch(cmd.raw(), grid.columns, grid.rows, 1)
		};

		raptor_gfx::barrier::buffer_compute_to_fragment(
			gfx.device(),
			cmd.raw(),
			&buffers.light_grid,
		);
		raptor_gfx::barrier::buffer_compute_to_fragment(
			gfx.device(),
			cmd.raw(),
			&buffers.light_index_list,
		);
		raptor_gfx::barrier::buffer_compute_to_fragment(
			gfx.device(),
			cmd.raw(),
			&buffers.decal_mask,
		);
	}

	pub fn render_early_frame_effects(&self, gfx: &Gfx, camera: &CameraCore) {
		let cmd = gfx.frame_cmd();

		self.ssao_pass.begin(gfx, cmd.raw());
		self.pipelines.bind_named(gfx, cmd, PipelineName::Ssao);

		let size = self
			.ssao_pass
			.target_image(ImageFormat::R8UNorm)
			.map_or((1, 1), |image| {
				(image.fields().width, image.fields().height)
			});

		let push = SsaoPushConstants {
			inv_projection: camera.inv_projection,
			projection: camera.projection,
			view: camera.view,
			render_size: [size.0 as f32, size.1 as f32],
			radius: cvar::float("r_ssao_radius", DEFAULT_SSAO_RADIUS),
			bias: cvar::float("r_ssao_bias", DEFAULT_SSAO_BIAS),
			strength: cvar::float("r_ssao_strength", DEFAULT_SSAO_STRENGTH),
			power: cvar::float("r_ssao_power", DEFAULT_SSAO_POWER),
			floor: cvar::float("r_ssao_floor", DEFAULT_SSAO_FLOOR),
			padding: [0.0],
		};

		self.draw_fullscreen(gfx, cmd, PipelineName::Ssao, bytemuck::bytes_of(&push));

		self.ssao_pass.end(gfx, cmd.raw());

		self.ssao_blur_pass.begin(gfx, cmd.raw());
		self.pipelines.bind_named(gfx, cmd, PipelineName::SsaoBlur);

		let blur_size = self
			.ssao_blur_pass
			.target_image(ImageFormat::R8UNorm)
			.map_or((1, 1), |image| {
				(image.fields().width, image.fields().height)
			});

		let blur = forward::ssao_blur_push([blur_size.0, blur_size.1]);

		let bytes = [
			blur.screen_size[0],
			blur.screen_size[1],
			blur.texel_size[0],
			blur.texel_size[1],
			blur.depth_sharpness,
			0.0,
			0.0,
			0.0,
		];

		self.draw_fullscreen(
			gfx,
			cmd,
			PipelineName::SsaoBlur,
			bytemuck::cast_slice(&bytes),
		);

		self.ssao_blur_pass.end(gfx, cmd.raw());
	}

	fn draw_fullscreen(&self, gfx: &Gfx, cmd: &CommandBuffer, name: PipelineName, push: &[u8]) {
		if let Some(slot) = self.pipelines.slot(gfx, name.handle())
			&& slot.is_built()
		{
			self.pipelines.push_constants(gfx, cmd, slot, PIXEL, push);

			// SAFETY: the command buffer is recording inside a render pass with the pipeline bound.
			unsafe { gfx.device().raw().cmd_draw(cmd.raw(), 3, 1, 0, 0) };
		}
	}

	pub fn render_composition(&self, gfx: &Gfx) {
		let cmd = gfx.frame_cmd();

		let name = if gfx.state().tonemapper() == 1 {
			PipelineName::CompositionAgx
		} else {
			PipelineName::Composition
		};

		self.pipelines.bind_named(gfx, cmd, name);

		let extent = gfx.swapchain().extent();

		let push = CompositionPushConstants {
			frame_extent: [extent.0, extent.1],
			padding: [0; 2],
		};

		self.draw_fullscreen(gfx, cmd, name, bytemuck::bytes_of(&push));
	}

	pub fn end_geometry(&self, gfx: &Gfx) {
		self.forward_pass.end(gfx, gfx.frame_cmd().raw());
		gfx.mark_gpu(GpuMarker::Forward);
	}

	pub fn finish_frame(&self, gfx: &Gfx, between: impl FnOnce()) {
		between();

		let cmd = gfx.frame_cmd();

		self.comp_pass.begin(gfx, cmd.raw());
		self.render_composition(gfx);
		self.comp_pass.end(gfx, cmd.raw());
		gfx.mark_gpu(GpuMarker::Composition);
	}

	pub fn draw_push_constants(&self, gfx: &Gfx) -> DrawPushConstants {
		let extent = gfx.swapchain().extent();

		DrawPushConstants {
			tile_columns: self.light_tile_columns,
			tile_rows: self.light_tile_rows,
			target_size: [extent.0, extent.1],
			pre_exposure: gfx.state().pre_exposure(),
			..DrawPushConstants::default()
		}
	}
}
