use ash::vk::{self, Handle};
use raptor_anim::NO_BONE;
use raptor_asset::materials::MaterialId;
use raptor_entity::CameraCore;
use raptor_entity::object_core::{
	FLAG_HAS_MESH, FLAG_PHYSICS_ENABLED, FLAG_SHADOW_CASTER, LAYER_WORLD, NO_BONES, NO_ID,
};
use raptor_gfx::{CommandBuffer, Gfx};
use raptor_math::frustum::{ALL_PLANES, SIDE_PLANES};
use raptor_math::{Frustum, Mat4f, Obb, Vec3f};
use raptor_physics::Backend;
use raptor_render::light::LightId;
use raptor_render::light_gpu::ShadowInputs;
use raptor_render::names::{Features, PipelineHandle, PipelinePass};
use raptor_render::push_constants::{
	DRAW_DEBUG_IRRADIANCE, DRAW_DEBUG_PROBE_VISIBILITY, DRAW_DEBUG_REFLECTION,
	DRAW_DEBUG_REFLECTION_COVERAGE, DRAW_NO_DECALS, DRAW_NO_PROBES, DRAW_NO_REFLECTION_PROBES,
	DRAW_PROBE_BOUNCE, DRAW_PROBE_CAPTURE, DRAW_REFLECTION_CAPTURE, DrawPushConstants,
	ShadowPushConstants,
};
use raptor_render::render_list::{INVALID_OBJECT, RenderList};
use raptor_render::render_lists::{CullStats, ListBuilder, ListSinks};
use raptor_render::renderer::LightCullOverride;
use raptor_render::shadow_atlas::{self, Region};
use raptor_render::world_logic::{
	CasterState, far_to_near_order, skinned_caster_reaches_sphere, spot_bake_hash, visible_tiles,
};
use raptor_scene::World;
use raptor_scene::scene::{FLAG_READY_TO_RENDER, ObjectId, SkeletonRef};
use raptor_world::{GLOBAL_TILE, NULL_TILE};

use crate::gpu::{Draw, Gpu};

const SKINNED_CASTER_PADDING: f32 = 0.3;
const PROBE_SHADOW_EDGE_MARGIN: f32 = 0.5;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CaptureState
{
	pub capturing: bool,
	pub bounce: bool,
	pub reflection: bool,
}

impl CaptureState
{
	pub fn of(probes: &raptor_render::probes::ProbeManager) -> Self
	{
		Self {
			capturing: probes.is_capturing_faces(),
			bounce: probes.is_capturing_bounce(),
			reflection: probes.is_capturing_reflection(),
		}
	}
}

const VERTEX_FRAGMENT: vk::ShaderStageFlags =
	vk::ShaderStageFlags::from_raw(vk::ShaderStageFlags::VERTEX.as_raw() | vk::ShaderStageFlags::FRAGMENT.as_raw());

struct SpotBake
{
	light: LightId,
	first_caster: usize,
	caster_count: usize,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FrameStats
{
	pub listed: u32,
	pub tested: u32,
	pub culled: u32,
	pub lights: u32,
	pub lights_total: u32,
}

#[derive(Default)]
pub struct WorldRenderer
{
	render_list: RenderList,
	visible_tiles: Vec<u32>,
	cull: CullStats,
	spot_bakes: Vec<SpotBake>,
	spot_casters: Vec<ObjectId>,
	sun_light_slot: Option<u32>,
	warned_spot_tiles: bool,
}

fn camera_matrix(camera: &CameraCore, layer: u32) -> [f32; 16]
{
	if layer == LAYER_WORLD {
		camera.camera_matrix
	} else {
		camera.weapon_camera_matrix
	}
}

fn position_of(camera: &CameraCore) -> Vec3f
{
	Vec3f::new(camera.position[0], camera.position[1], camera.position[2])
}

fn flag_or(flags: &mut u32, condition: bool, flag: u32)
{
	if condition {
		*flags |= flag;
	}
}

impl WorldRenderer
{
	pub fn new() -> Self
	{
		Self::default()
	}

	pub fn stats(&self, gpu: &Gpu) -> FrameStats
	{
		FrameStats {
			listed: self.render_list.count() as u32,
			tested: self.cull.tested,
			culled: self.cull.culled,
			lights: gpu.render.light_list.len() as u32,
			lights_total: gpu.render.lights.iter().count() as u32,
		}
	}

	pub fn invalidate_object(&mut self, object: ObjectId)
	{
		self.render_list.invalidate(object);
	}

	fn clear_render_list(&mut self, draw: &Draw)
	{
		for pass in [PipelinePass::Forward, PipelinePass::ForwardBlend] {
			for pipeline in draw.pipelines.pass_pipelines(pass) {
				self.render_list.clear_section(pipeline);
			}
		}

		self.render_list.clear_section(draw.shadow_list_pipeline());
	}

	fn fill_list_from_tiles<B: Backend>(
		&mut self,
		world: &mut World<B>,
		draw: &Draw,
		tiles: &[u32],
		include_global: bool,
		frustum: Option<Frustum>,
	)
	{
		let shadow_pipeline = draw.shadow_list_pipeline();

		let mut builder = ListBuilder::new(world.scene.objects(), frustum, shadow_pipeline.0);

		let render_list = &mut self.render_list;

		let mut pipeline_for_material =
			|material: u32| draw.pipeline_for_material(MaterialId(material)).0;
		let mut add = |pipeline: u32, object: u32| render_list.add(PipelineHandle(pipeline), object);

		let mut sinks = ListSinks {
			pipeline_for_material: &mut pipeline_for_material,
			add: &mut add,
		};

		for tile in tiles {
			if let Some(objects) = world.scene.grid.tile_objects(*tile) {
				builder.add_tile(objects, &mut sinks);
			}
		}

		if include_global && let Some(objects) = world.scene.grid.tile_objects(GLOBAL_TILE) {
			builder.add_tile(objects, &mut sinks);
		}

		self.cull = builder.stats;
	}

	fn add_tile_lights<B: Backend>(
		world: &World<B>,
		gpu: &mut Gpu,
		tile: u32,
		frustum: Option<&Frustum>,
	)
	{
		let Some(lights) = world.scene.grid.tile_lights(tile) else {
			return;
		};

		for &id in lights {
			if id == raptor_world::grid::EMPTY {
				continue;
			}

			let light_id = LightId(id);

			if let Some(frustum) = frustum
				&& let Some(light) = gpu.render.lights.get(light_id)
				&& light.is_outside_frustum(frustum)
			{
				continue;
			}

			gpu.render.light_list.add(light_id);
		}
	}

	fn cull_world_tiles<B: Backend>(&mut self, world: &mut World<B>, gpu: &mut Gpu, camera: &CameraCore)
	{
		let view_projection = Mat4f::from_rows(&camera.camera_matrix);
		let frustum = Frustum::from_view_projection(&view_projection);

		let frustum_cull = raptor_core::cvar::int("r_frustum_cull", 1) != 0;
		let baking = gpu.render.probes.is_baking();
		let light_frustum = (frustum_cull && !baking).then_some(frustum);

		gpu.render.light_list.clear();

		Self::add_tile_lights(world, gpu, GLOBAL_TILE, light_frustum.as_ref());

		let tiles = visible_tiles(&world.scene.grid, &view_projection);

		self.visible_tiles = tiles.clone();

		for tile in &tiles {
			Self::add_tile_lights(world, gpu, *tile, light_frustum.as_ref());
		}

		let draw = gpu.draw();

		self.clear_render_list(&draw);
		self.fill_list_from_tiles(world, &draw, &tiles, true, frustum_cull.then_some(frustum));

		let needs_all_lights = gpu.render.probes.is_baking();

		let unculled: Vec<LightId> = gpu
			.render
			.lights
			.iter()
			.filter(|(id, _)| needs_all_lights || world.scene.grid.light_tile(id.0) == NULL_TILE)
			.map(|(id, _)| id)
			.collect();

		for id in unculled {
			gpu.render.light_list.add(id);
		}
	}

	fn is_ready<B: Backend>(
		world: &mut World<B>,
		draw: &Draw,
		object: ObjectId,
		require_material: bool,
	) -> bool
	{
		let Some(core) = world.scene.core(object) else {
			return false;
		};

		if core.has_flag(FLAG_READY_TO_RENDER) {
			return true;
		}

		let material_id = core.material_id;

		let Some(mesh) = world.scene.mesh(object) else {
			return false;
		};

		if !draw.meshes.is_ready(mesh.id) {
			return false;
		}

		let _ = require_material;

		let Some(material) = draw.materials.get(MaterialId(material_id)) else {
			return false;
		};

		if !material.is_ready() {
			return false;
		}

		if let Some(core) = world.scene.core_mut(object) {
			core.set_flag(FLAG_READY_TO_RENDER, true);
		}

		raptor_core::log_info!(Render; "Object {} is now ready to render.", world.scene.name(object));

		world.scene.merge_into_parent(object);

		let unlit = world
			.scene
			.core(object)
			.is_some_and(|core| core.has_flag(raptor_scene::scene::FLAG_UNLIT));

		if unlit {
			material.with_record(|record| record.set_unlit(true));
		}

		true
	}

	fn update_animation<B: Backend>(world: &mut World<B>, gfx: &Gfx, object: ObjectId)
	{
		let skeleton: Option<SkeletonRef> = world.scene.node(object).and_then(|node| node.skeleton);

		let base = match skeleton.and_then(|skeleton| world.skeleton_mut(skeleton)) {
			Some(skeleton) => {
				raptor_asset::skeleton_update::update_skeleton(gfx, skeleton, gfx.state().delta_time());

				skeleton.bone_buffer_base()
			}
			None => NO_BONE,
		};

		if let Some(core) = world.scene.core_mut(object) {
			core.bone_buffer_base = if base == NO_BONE { NO_BONES } else { base };
		}
	}

	fn update_object<B: Backend>(world: &mut World<B>, draw: &Draw, object: ObjectId)
	{
		world.scene.update(object);

		Self::update_animation(world, draw.gfx, object);

		let frame = draw.gfx.frame_number();

		if let Some(entity) = world.scene.entity_mut(object) {
			entity.update_matrix_if_out_of_date();

			if entity.submit_needed(frame) {
				draw.objects.submit(frame, object, &entity.matrix);
			}
		}
	}

	fn has_bones_for_draw<B: Backend>(world: &World<B>, object: ObjectId) -> bool
	{
		let Some(core) = world.scene.core(object) else {
			return false;
		};

		core.has_bones_for_draw(world.scene.is_skinned(object))
	}

	fn bind_material(draw: &Draw, cmd: &CommandBuffer, slot: &raptor_render::pipeline_cache::PipelineSlot, material: MaterialId)
	{
		let gpu = draw.material_gpu();
		let (bone, light) = {
			let buffers = draw.gfx.buffers();
			let frame = draw.gfx.frame_number();

			let bone = buffers
				.bone
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner())
				.base_offset(frame);
			let light = buffers
				.light
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner())
				.base_offset(frame);

			(bone, light)
		};

		let layout = vk::PipelineLayout::from_raw(slot.layout);

		let mut bound = draw.materials.bind(
			&gpu,
			material,
			cmd.raw(),
			vk::PipelineBindPoint::GRAPHICS,
			layout,
			[bone, light],
		);

		let mut used = material;

		if !bound {
			used = MaterialId::NULL;

			bound = draw.materials.bind(
				&gpu,
				used,
				cmd.raw(),
				vk::PipelineBindPoint::GRAPHICS,
				layout,
				[bone, light],
			);
		}

		if bound {
			let double_sided = draw
				.materials
				.get(used)
				.is_some_and(|material| material.is_double_sided());

			draw.pipelines.set_double_sided(draw.gfx, cmd, slot, double_sided);
		}
	}

	fn draw_mesh<B: Backend>(world: &World<B>, draw: &Draw, cmd: &CommandBuffer, object: ObjectId)
	{
		let Some(mesh) = world.scene.mesh(object).and_then(|mesh| draw.meshes.get(mesh.id)) else {
			return;
		};

		let slots = world
			.scene
			.core(object)
			.map_or(0, |core| u32::from(core.instance_slots_in_use));

		mesh.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.render(draw.gfx, cmd, slots + 1);
	}

	fn probe_flags(draw: &Draw, capture: CaptureState) -> (u32, f32)
	{
		let state = draw.gfx.state();
		let mut flags = 0;

		let capturing = capture.capturing;

		if capturing {
			flags |= DRAW_PROBE_CAPTURE;

			flag_or(&mut flags, capture.bounce, DRAW_PROBE_BOUNCE);
			flag_or(
				&mut flags,
				capture.reflection,
				DRAW_REFLECTION_CAPTURE,
			);
		}

		flag_or(&mut flags, state.only_render_probes(), DRAW_DEBUG_IRRADIANCE);
		flag_or(
			&mut flags,
			state.render_probe_visibility(),
			DRAW_DEBUG_PROBE_VISIBILITY,
		);
		flag_or(
			&mut flags,
			state.disable_reflection_probes(),
			DRAW_NO_REFLECTION_PROBES,
		);
		flag_or(&mut flags, state.disable_probes() && !capturing, DRAW_NO_PROBES);
		flag_or(&mut flags, state.disable_decals(), DRAW_NO_DECALS);
		flag_or(
			&mut flags,
			!capturing && state.reflection_debug_view() == 1,
			DRAW_DEBUG_REFLECTION,
		);
		flag_or(
			&mut flags,
			!capturing && state.reflection_debug_view() == 2,
			DRAW_DEBUG_REFLECTION_COVERAGE,
		);

		let pre_exposure = if capturing { 1.0 } else { state.pre_exposure() };

		(flags, pre_exposure)
	}

	#[allow(clippy::too_many_arguments)]
	fn render_shallow<B: Backend>(
		world: &mut World<B>,
		draw: &Draw,
		capture: CaptureState,
		camera: &CameraCore,
		object: ObjectId,
		slot: &raptor_render::pipeline_cache::PipelineSlot,
	)
	{
		Self::update_object(world, draw, object);

		if !Self::is_ready(world, draw, object, true) || !Self::has_bones_for_draw(world, object) {
			return;
		}

		let Some(core) = world.scene.core(object) else {
			return;
		};

		let (layer, material, bone_base) = (core.layer, core.material_id, core.bone_buffer_base);

		let mut constants: DrawPushConstants = draw.renderer.draw_push_constants(draw.gfx);

		constants.object_id = object;
		constants.material_index = material;
		constants.bone_base = bone_base;

		let (flags, pre_exposure) = Self::probe_flags(draw, capture);

		constants.flags = flags;
		constants.pre_exposure = pre_exposure;

		if layer != LAYER_WORLD {
			constants.flags |= DRAW_NO_DECALS;
		}

		constants.camera_matrix = camera_matrix(camera, layer);
		constants.eye_position = [camera.position[0], camera.position[1], camera.position[2], 1.0];

		let cmd = draw.gfx.frame_cmd();

		draw.pipelines.push_constants(
			draw.gfx,
			cmd,
			slot,
			VERTEX_FRAGMENT,
			bytemuck::bytes_of(&constants),
		);

		Self::bind_material(draw, cmd, slot, MaterialId(material));

		Self::draw_mesh(world, draw, cmd, object);
	}

	fn execute_render_list<B: Backend>(
		&self,
		world: &mut World<B>,
		draw: &Draw,
		capture: CaptureState,
		list: PipelineHandle,
		draw_pipeline: PipelineHandle,
		camera: &CameraCore,
	)
	{
		if !draw_pipeline.is_valid() {
			return;
		}

		let Some(slot) = draw.pipelines.slot(draw.gfx, draw_pipeline) else {
			return;
		};

		if !slot.is_built() {
			return;
		}

		let cmd = draw.gfx.frame_cmd();

		draw.pipelines.bind_pipeline(draw.gfx, cmd, slot);

		draw.renderer.bind_persistent(
			draw.gfx,
			cmd,
			slot,
			crate::object_buffer::ObjectBuffer::base_offset(draw.gfx.frame_number()),
		);

		let capturing = capture.capturing;

		for object in self.render_list.valid(list).collect::<Vec<_>>() {
			if !world.scene.is_used(object) {
				continue;
			}

			if capturing && !world.scene.core(object).is_some_and(|core| core.is_probe_visible()) {
				continue;
			}

			Self::render_shallow(world, draw, capture, camera, object, slot);
		}
	}

	fn execute_forward_render_lists<B: Backend>(
		&self,
		world: &mut World<B>,
		draw: &Draw,
		capture: CaptureState,
		camera: &CameraCore,
	)
	{
		let capturing = capture.capturing;
		let debug_view = draw.gfx.state().has_debug_view();

		for pipeline in draw.pipelines.pass_pipelines(PipelinePass::Forward) {
			let mut draw_pipeline = pipeline;

			if capturing {
				draw_pipeline = draw.pipelines.get_or_create_variant_in_pass(
					draw.gfx,
					pipeline,
					PipelinePass::ForwardCapture,
				);
			} else if debug_view {
				draw_pipeline = draw.pipelines.get_or_create_variant_in_pass(
					draw.gfx,
					pipeline,
					PipelinePass::ForwardDebug,
				);
			}

			self.execute_render_list(world, draw, capture, pipeline, draw_pipeline, camera);
		}
	}

	fn execute_transparent_render_lists<B: Backend>(
		&self,
		world: &mut World<B>,
		draw: &Draw,
		capture: CaptureState,
		camera: &CameraCore,
	)
	{
		let camera_position = position_of(camera);

		let mut entries: Vec<(ObjectId, PipelineHandle)> = Vec::new();
		let mut centers: Vec<f32> = Vec::new();

		for pipeline in draw.pipelines.pass_pipelines(PipelinePass::ForwardBlend) {
			for object in self.render_list.valid(pipeline) {
				let (Some(position), Some(bounds)) =
					(world.scene.position(object), world.scene.bounds(object))
				else {
					continue;
				};

				let size = bounds.size();

				centers.extend_from_slice(&[
					position[0] + bounds.min.x + size.x * 0.5,
					position[1] + bounds.min.y + size.y * 0.5,
					position[2] + bounds.min.z + size.z * 0.5,
				]);

				entries.push((object, pipeline));
			}
		}

		if entries.is_empty() {
			return;
		}

		let order = far_to_near_order(&centers, camera_position.to_array());

		let debug_view = draw.gfx.state().has_debug_view();
		let cmd = draw.gfx.frame_cmd();

		let mut bound: Option<PipelineHandle> = None;
		let mut slot = None;

		for index in order {
			let (object, pipeline) = entries[index as usize];

			if bound != Some(pipeline) {
				let draw_pipeline = if debug_view {
					draw.pipelines.get_or_create_variant_in_pass(
						draw.gfx,
						pipeline,
						PipelinePass::ForwardBlendDebug,
					)
				} else {
					pipeline
				};

				if !draw_pipeline.is_valid() {
					continue;
				}

				let Some(found) = draw.pipelines.slot(draw.gfx, draw_pipeline) else {
					continue;
				};

				if !found.is_built() {
					continue;
				}

				draw.pipelines.bind_pipeline(draw.gfx, cmd, found);
				draw.renderer.bind_persistent(
					draw.gfx,
					cmd,
					found,
					crate::object_buffer::ObjectBuffer::base_offset(draw.gfx.frame_number()),
				);

				bound = Some(pipeline);
				slot = Some(found);
			}

			if let Some(slot) = slot {
				Self::render_shallow(world, draw, capture, camera, object, slot);
			}
		}
	}

	fn execute_prepass_render_list<B: Backend>(
		&self,
		world: &mut World<B>,
		draw: &Draw,
		capture: CaptureState,
		forward_pipeline: PipelineHandle,
		camera: &CameraCore,
	)
	{
		let prepass = draw.pipelines.get_or_create_variant_in_pass(
			draw.gfx,
			forward_pipeline,
			PipelinePass::Depth,
		);

		if !prepass.is_valid() {
			return;
		}

		let Some(slot) = draw.pipelines.slot(draw.gfx, prepass) else {
			return;
		};

		if !slot.is_built() {
			return;
		}

		let cmd = draw.gfx.frame_cmd();

		draw.pipelines.bind_pipeline(draw.gfx, cmd, slot);
		draw.renderer.bind_persistent_slim(
			draw.gfx,
			cmd,
			slot,
			crate::object_buffer::ObjectBuffer::base_offset(draw.gfx.frame_number()),
		);

		for object in self.render_list.valid(forward_pipeline).collect::<Vec<_>>() {
			if !world.scene.is_used(object) {
				continue;
			}

			Self::update_object(world, draw, object);

			let Some(core) = world.scene.core(object) else {
				continue;
			};

			let (layer, material, bone_base) = (core.layer, core.material_id, core.bone_buffer_base);

			let mut constants = draw.renderer.draw_push_constants(draw.gfx);

			constants.object_id = object;
			constants.bone_base = bone_base;
			constants.camera_matrix = camera_matrix(camera, layer);
			constants.material_index = material;

			draw.pipelines.push_constants(
				draw.gfx,
				cmd,
				slot,
				VERTEX_FRAGMENT,
				bytemuck::bytes_of(&constants),
			);

			Self::bind_material(draw, cmd, slot, MaterialId(material));

			if Self::is_ready(world, draw, object, false) && Self::has_bones_for_draw(world, object)
			{
				Self::draw_mesh(world, draw, cmd, object);
			}
		}
	}

	fn is_masked<B: Backend>(world: &World<B>, draw: &Draw, object: ObjectId) -> bool
	{
		if world.scene.is_skinned(object) {
			return false;
		}

		let Some(core) = world.scene.core(object) else {
			return false;
		};

		draw.materials
			.get(MaterialId(core.material_id))
			.is_some_and(|material| {
				material.with_record(|record| {
					record.has_flag(raptor_render::material::FLAG_ALPHA_MASK)
				}) && material.is_ready()
			})
	}

	fn begin_shadow_region(&self, gpu: &mut Gpu, region: Region)
	{
		let pipelines = std::sync::Arc::clone(&gpu.render.pipelines);
		let gfx = std::sync::Arc::clone(&gpu.gfx);

		gpu.render.shadow.begin_region(&gfx, &pipelines, region);

		let shadow_list = gpu.draw().shadow_list_pipeline();

		if let Some(slot) = pipelines.slot(&gfx, shadow_list) {
			gpu.render.renderer.bind_persistent_slim(
				&gfx,
				gfx.frame_cmd(),
				slot,
				crate::object_buffer::ObjectBuffer::base_offset(gfx.frame_number()),
			);
		}
	}

	fn draw_shadow_caster<B: Backend>(
		world: &mut World<B>,
		gpu: &Gpu,
		bound: &mut PipelineHandle,
		constants: &mut ShadowPushConstants,
		object: ObjectId,
	)
	{
		let draw = gpu.draw();

		let masked = Self::is_masked(world, &draw, object);
		let skinned = world.scene.is_skinned(object);

		let features = if skinned {
			Features::SKINNED
		} else if masked {
			Features::ALPHA_MASK
		} else {
			Features::NONE
		};

		let wanted = draw
			.pipelines
			.get_or_create_variant(draw.gfx, PipelinePass::Shadow, features);

		let cmd = draw.gfx.frame_cmd();

		if wanted != *bound {
			gpu.render.shadow.bind_pipeline(draw.gfx, draw.pipelines, wanted);

			*bound = wanted;

			if let Some(slot) = draw.pipelines.slot(draw.gfx, wanted) {
				draw.renderer.bind_persistent_slim(
					draw.gfx,
					cmd,
					slot,
					crate::object_buffer::ObjectBuffer::base_offset(draw.gfx.frame_number()),
				);
			}
		}

		let Some(slot) = draw.pipelines.slot(draw.gfx, *bound) else {
			return;
		};

		let Some(core) = world.scene.core(object) else {
			return;
		};

		constants.object_index = object;
		constants.bone_base = core.bone_buffer_base;

		let material = MaterialId(core.material_id);

		if masked || skinned {
			Self::bind_material(&draw, cmd, slot, material);
		}

		draw.pipelines.push_constants(
			draw.gfx,
			cmd,
			slot,
			vk::ShaderStageFlags::VERTEX,
			bytemuck::bytes_of(constants),
		);

		if Self::is_ready(world, &draw, object, false) && Self::has_bones_for_draw(world, object) {
			Self::draw_mesh(world, &draw, cmd, object);
		}
	}

	fn execute_shadow_render_list<B: Backend>(
		&self,
		world: &mut World<B>,
		gpu: &Gpu,
		shadow_camera: &CameraCore,
	)
	{
		let draw = gpu.draw();
		let shadow_list = draw.shadow_list_pipeline();

		let mut constants = ShadowPushConstants {
			camera_matrix: shadow_camera.camera_matrix,
			..ShadowPushConstants::default()
		};

		let capturing = CaptureState::of(&gpu.render.probes).capturing;

		let frustum = Frustum::from_view_projection(&Mat4f::from_rows(&shadow_camera.camera_matrix));
		let cull = raptor_core::cvar::int("r_frustum_cull", 1) != 0;

		let mut bound = shadow_list;

		for object in self.render_list.valid(shadow_list).collect::<Vec<_>>() {
			if !world.scene.is_used(object) {
				continue;
			}

			if capturing && !world.scene.core(object).is_some_and(|core| core.is_probe_visible()) {
				continue;
			}

			if cull && Self::outside_frustum(world, object, &frustum, SIDE_PLANES) {
				continue;
			}

			Self::update_object(world, &draw, object);

			Self::draw_shadow_caster(world, gpu, &mut bound, &mut constants, object);
		}
	}

	fn outside_frustum<B: Backend>(
		world: &mut World<B>,
		object: ObjectId,
		frustum: &Frustum,
		planes: u32,
	) -> bool
	{
		if !world.scene.can_be_frustum_culled(object) {
			return false;
		}

		let (Some(bounds), Some(matrix)) = (world.scene.bounds(object), world.scene.world_matrix(object))
		else {
			return false;
		};

		!frustum.intersects_obb(&Obb::from_local_bounds(&bounds, &matrix), planes)
	}

	fn gather_spot_casters<B: Backend>(
		&self,
		world: &mut World<B>,
		draw: &Draw,
		center: Vec3f,
		radius: f32,
		out: &mut Vec<ObjectId>,
	)
	{
		let first = out.len();

		let extent = Vec3f::splat(radius);

		let grid = &world.scene.grid;

		let min_tile = grid.tile_to_xy(grid.world_to_tile((center - extent).to_array()));
		let max_tile = grid.tile_to_xy(grid.world_to_tile((center + extent).to_array()));

		let mut tiles = Vec::new();

		for y in min_tile[1]..=max_tile[1] {
			for x in min_tile[0]..=max_tile[0] {
				tiles.push(grid.tile_from_xy([x, y]));
			}
		}

		tiles.push(GLOBAL_TILE);

		for tile in tiles {
			let ids: Vec<u32> = world
				.scene
				.grid
				.tile_objects(tile)
				.map(<[u32]>::to_vec)
				.unwrap_or_default();

			for id in ids {
				if id == raptor_world::grid::EMPTY || !world.scene.is_used(id) {
					continue;
				}

				if world
					.scene
					.core(id)
					.is_some_and(|core| core.has_flag(FLAG_SHADOW_CASTER))
				{
					Self::add_caster_recursive(world, draw, id, first, center, radius, out);
				}
			}
		}
	}

	fn add_caster_recursive<B: Backend>(
		world: &mut World<B>,
		draw: &Draw,
		id: ObjectId,
		first: usize,
		center: Vec3f,
		radius: f32,
		out: &mut Vec<ObjectId>,
	)
	{
		if id == NO_ID || !world.scene.is_used(id) || out[first.min(out.len())..].contains(&id) {
			return;
		}

		let skinned = world.scene.is_skinned(id);

		if !skinned || Self::skinned_reaches(world, draw, id, center, radius) {
			out.push(id);
		}

		let children: Vec<u32> = world
			.scene
			.core(id)
			.map(|core| core.children().to_vec())
			.unwrap_or_default();

		for child in children {
			Self::add_caster_recursive(world, draw, child, first, center, radius, out);
		}
	}

	fn skinned_reaches<B: Backend>(
		world: &mut World<B>,
		draw: &Draw,
		id: ObjectId,
		center: Vec3f,
		radius: f32,
	) -> bool
	{
		let Some(skeleton) = world.scene.node(id).and_then(|node| node.skeleton) else {
			return false;
		};

		Self::update_animation(world, draw.gfx, id);

		if !Self::has_bones_for_draw(world, id) {
			return false;
		}

		let (Some(matrix), Some(scale)) = (world.scene.world_matrix(id), world.scene.scale(id)) else {
			return false;
		};

		let Some(skeleton) = world.skeleton(skeleton) else {
			return false;
		};

		let fields = skeleton.fields();

		skinned_caster_reaches_sphere(
			&matrix.to_rows(),
			scale,
			[fields.pose_center[0], fields.pose_center[1], fields.pose_center[2]],
			fields.pose_radius,
			SKINNED_CASTER_PADDING,
			center.to_array(),
			radius,
		)
	}

	fn update_spot_shadows<B: Backend>(&mut self, world: &mut World<B>, gpu: &mut Gpu)
	{
		self.spot_bakes.clear();
		self.spot_casters.clear();

		let lights: Vec<LightId> = gpu.render.light_list.valid().collect();

		for id in lights {
			let Some(light) = gpu.render.lights.get(id) else {
				continue;
			};

			if light.kind() != raptor_render::light::LightKind::Spot {
				continue;
			}

			if !light.casts_shadows() {
				let atlas = &mut gpu.render.shadow.state;

				if let Some(light) = gpu.render.lights.get_mut(id) {
					light.release_shadow_tile(atlas);
				}

				continue;
			}

			if !light.is_enabled() {
				continue;
			}

			let (position, radius, has_tile) = (
				light.position(),
				light.radius(),
				light.core.shadow_atlas_tile != raptor_entity::light_core::NO_TILE,
			);

			if !has_tile {
				match gpu.render.shadow.state.allocate_spot_tile() {
					Some(tile) => {
						if let Some(light) = gpu.render.lights.get_mut(id) {
							light.core.shadow_atlas_tile = tile;
						}
					}
					None => {
						if !std::mem::replace(&mut self.warned_spot_tiles, true) {
							raptor_core::log_warn!(Render; "Shadow atlas is out of spot light tiles, lights will not cast shadows");
						}

						continue;
					}
				}
			}

			let Some(light) = gpu.render.lights.get(id) else {
				continue;
			};

			let tile = light.core.shadow_atlas_tile;
			let shadow_matrix = light.shadow_matrix();

			let first = self.spot_casters.len();

			let draw = gpu.draw();
			let mut casters = std::mem::take(&mut self.spot_casters);

			self.gather_spot_casters(world, &draw, position, radius, &mut casters);

			let mut states = Vec::new();

			for caster in &casters[first..] {
				let mesh = world.scene.mesh(*caster);
				let drawable = mesh.is_some() && Self::is_ready(world, &draw, *caster, false);

				let pose = if world.scene.is_skinned(*caster) {
					world
						.scene
						.node(*caster)
						.and_then(|node| node.skeleton)
						.and_then(|skeleton| world.skeleton(skeleton))
						.map(|skeleton| {
							(
								skeleton.fields().pose_hash,
								Self::has_bones_for_draw(world, *caster),
							)
						})
				} else {
					None
				};

				states.push(CasterState {
					id: *caster,
					mesh: mesh.map_or(0, |mesh| u64::from(mesh.id)),
					drawable,
					world_matrix: world
						.scene
						.world_matrix(*caster)
						.map_or([0.0; 16], |matrix| matrix.to_rows()),
					pose,
				});
			}

			let hash = spot_bake_hash(
				gpu.render.shadow.state.generation(),
				tile,
				&shadow_matrix.to_rows(),
				&states,
			);

			let existing = gpu
				.render
				.lights
				.get(id)
				.map_or(0, |light| light.core.shadow_bake_hash);

			if hash == existing {
				casters.truncate(first);
				self.spot_casters = casters;

				continue;
			}

			if let Some(light) = gpu.render.lights.get_mut(id) {
				light.core.shadow_matrix = shadow_matrix.to_rows();
				light.core.shadow_bake_hash = hash;
			}

			let count = casters.len() - first;

			self.spot_casters = casters;

			self.spot_bakes.push(SpotBake {
				light: id,
				first_caster: first,
				caster_count: count,
			});
		}
	}

	fn bake_spot_shadows<B: Backend>(&mut self, world: &mut World<B>, gpu: &mut Gpu)
	{
		let bakes = std::mem::take(&mut self.spot_bakes);

		for bake in &bakes {
			let Some(light) = gpu.render.lights.get(bake.light) else {
				continue;
			};

			let (tile, matrix) = (light.core.shadow_atlas_tile, light.core.shadow_matrix);

			let Some(region) = shadow_atlas::spot_tile_region(tile) else {
				continue;
			};

			self.begin_shadow_region(gpu, region);

			let mut constants = ShadowPushConstants {
				camera_matrix: matrix,
				..ShadowPushConstants::default()
			};

			let mut bound = gpu.draw().shadow_list_pipeline();

			let casters: Vec<ObjectId> = self.spot_casters
				[bake.first_caster..bake.first_caster + bake.caster_count]
				.to_vec();

			for caster in casters {
				if !world.scene.is_used(caster) {
					continue;
				}

				if let Some(matrix) = world.scene.world_matrix(caster) {
					gpu.objects.submit(gpu.gfx.frame_number(), caster, &matrix.to_rows());
				}

				Self::draw_shadow_caster(world, gpu, &mut bound, &mut constants, caster);
			}

			gpu.render.shadow.end_region(&gpu.gfx);
		}

		self.spot_bakes = bakes;
		self.spot_bakes.clear();
	}

	fn submit_lights(&mut self, gpu: &mut Gpu, camera: &CameraCore, shadow_camera: &CameraCore)
	{
		self.sun_light_slot = None;

		let lights: Vec<LightId> = gpu.render.light_list.valid().collect();

		for id in lights {
			let slot = gpu
				.gfx
				.buffers()
				.light
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner())
				.slot_index();

			gpu.render.submit_light(id, camera, Some(shadow_camera));

			let after = gpu
				.gfx
				.buffers()
				.light
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner())
				.slot_index();

			let is_sun = gpu
				.render
				.lights
				.get(id)
				.is_some_and(|light| light.kind() == raptor_render::light::LightKind::Directional);

			if is_sun && after != slot && self.sun_light_slot.is_none() {
				self.sun_light_slot = Some(slot);
			}
		}

		let _ = ShadowInputs {
			directional_matrix: [0.0; 16],
			directional_rect: [0.0; 4],
			spot_rect: None,
		};
	}

	/// Draws the world for the frame: shadows, the depth prepass, light culling, SSAO and the forward pass.
	pub fn render<B: Backend>(&mut self, world: &mut World<B>, gpu: &mut Gpu, shadow_camera: &CameraCore)
	{
		let camera = world.player.camera.clone();

		self.cull_world_tiles(world, gpu, &camera);

		let view_tile = world.scene.grid.world_to_tile(position_of(&camera).to_array());

		if view_tile != world.scene.grid.view_tile() {
			world.scene.grid.set_view_tile(view_tile);
		}

		self.update_spot_shadows(world, gpu);
		self.submit_lights(gpu, &camera, shadow_camera);

		let sun_enabled = gpu
			.render
			.lights
			.directional()
			.and_then(|id| gpu.render.lights.get(id))
			.is_some_and(|light| light.is_enabled());

		if sun_enabled || !gpu.render.shadow.state.is_initialized() {
			self.begin_shadow_region(gpu, shadow_atlas::directional_region());

			if sun_enabled {
				self.execute_shadow_render_list(world, gpu, shadow_camera);
			}

			gpu.render.shadow.end_region(&gpu.gfx);
		}

		self.bake_spot_shadows(world, gpu);

		gpu.gfx.mark_gpu(raptor_gfx::GpuMarker::Shadows);

		gpu.render.renderer.begin_prepass(&gpu.gfx);

		{
			let draw = gpu.draw();

			for pipeline in draw.pipelines.pass_pipelines(PipelinePass::Forward) {
				self.execute_prepass_render_list(world, &draw, CaptureState::of(&gpu.render.probes), pipeline, &camera);
			}
		}

		gpu.render.renderer.prepass.end(&gpu.gfx, gpu.gfx.frame_cmd().raw());
		gpu.gfx.mark_gpu(raptor_gfx::GpuMarker::Prepass);

		gpu.render.update_decals(&camera);

		let decal_count = gpu.render.decals.visible_count();

		gpu.render
			.renderer
			.do_light_culling_pass(&gpu.gfx, &camera, None, None, decal_count);
		gpu.gfx.mark_gpu(raptor_gfx::GpuMarker::LightCulling);

		gpu.render.renderer.render_early_frame_effects(&gpu.gfx, &camera);
		gpu.gfx.mark_gpu(raptor_gfx::GpuMarker::Ssao);

		gpu.render.renderer.begin_geometry(&gpu.gfx);

		{
			let draw = gpu.draw();

			self.execute_forward_render_lists(world, &draw, CaptureState::of(&gpu.render.probes), &camera);
			self.execute_transparent_render_lists(world, &draw, CaptureState::of(&gpu.render.probes), &camera);
		}
	}

	pub fn draw_debug(&mut self, gpu: &mut Gpu, camera: &CameraCore)
	{
		let pipelines = std::sync::Arc::clone(&gpu.render.pipelines);
		let gfx = std::sync::Arc::clone(&gpu.gfx);

		gpu.render.debug.render(&gfx, &pipelines, camera);
	}

	fn render_capture_sun_shadows<B: Backend>(
		&mut self,
		world: &mut World<B>,
		gpu: &mut Gpu,
		center: Vec3f,
	) -> Option<(CameraCore, u32)>
	{
		let sun_id = gpu.render.lights.directional()?;
		let sun_direction = gpu.render.lights.get(sun_id)?.position().normalize();

		let mut camera = gpu.render.sun.camera.clone();

		raptor_render::shadow_directional::place_camera(
			&mut camera,
			gpu.render.sun.map_size[0] as f32,
			center,
			sun_direction,
		);

		let slot = gpu
			.gfx
			.buffers()
			.light
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.slot_index();

		let player_camera = world.player.camera.clone();

		gpu.render.submit_light(sun_id, &player_camera, Some(&camera));

		let after = gpu
			.gfx
			.buffers()
			.light
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.slot_index();

		if after == slot {
			return None;
		}

		self.begin_shadow_region(gpu, shadow_atlas::directional_region());
		self.execute_shadow_render_list(world, gpu, &camera);
		gpu.render.shadow.end_region(&gpu.gfx);

		Some((camera, slot))
	}

	/// Records the next batch of light probe captures, if a bake is running.
	pub fn render_probe_capture<B: Backend>(&mut self, world: &mut World<B>, gpu: &mut Gpu)
	{
		if !gpu.render.probes.is_capture_pending() {
			return;
		}

		let extent = gpu.render.probes.capture_extent();

		let saved = (
			gpu.render.renderer.light_tile_columns,
			gpu.render.renderer.light_tile_rows,
		);

		let draw = gpu.draw();

		self.clear_render_list(&draw);

		let grid_size = world.scene.grid.grid_size();
		let tiles: Vec<u32> = (0..grid_size[0] * grid_size[1]).collect();

		self.fill_list_from_tiles(world, &draw, &tiles, true, None);

		let sun_has_shadows = gpu
			.render
			.lights
			.directional()
			.and_then(|id| gpu.render.lights.get(id))
			.is_some_and(|light| light.is_enabled())
			&& self.sun_light_slot.is_some();

		let sun_slot = self.sun_light_slot;

		let light_count = gpu
			.gfx
			.buffers()
			.light
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.slot_index();

		let mut capture_shadow_camera: Option<CameraCore> = None;
		let mut light_override = LightCullOverride {
			light_count,
			replaced_light: u32::MAX,
			replacement_slot: u32::MAX,
		};

		let mut probes = std::mem::replace(
			&mut gpu.render.probes,
			raptor_render::probes::ProbeManager::new(Box::new(NullProbeGpu)),
		);

		let capture = gpu.render.capture.clone();

		let capture_state = CaptureState {
			capturing: true,
			bounce: probes.is_capturing_bounce(),
			reflection: probes.is_capturing_reflection(),
		};

		probes.record_capture_batch(&mut |camera, kind| {
			let center = position_of(camera);

			let needs_sun = capture_shadow_camera.as_ref().is_none_or(|existing| {
				!raptor_render::shadow_directional::is_well_covered(
					existing,
					center,
					PROBE_SHADOW_EDGE_MARGIN,
				)
			});

			if sun_has_shadows && needs_sun
				&& let Some((shadow_camera, slot)) = self.render_capture_sun_shadows(world, gpu, center)
			{
				light_override.replaced_light = sun_slot.unwrap_or(u32::MAX);
				light_override.replacement_slot = slot;
				capture_shadow_camera = Some(shadow_camera);
			}

			gpu.render.renderer.do_light_culling_pass(
				&gpu.gfx,
				camera,
				Some((extent[0], extent[1])),
				Some(light_override),
				0,
			);

			capture.with_stage(kind, |stage| {
				let cmd = gpu.gfx.frame_cmd();

				stage.begin(&gpu.gfx, cmd.raw());

				let draw = gpu.draw();

				self.execute_forward_render_lists(world, &draw, capture_state, camera);

				stage.end(&gpu.gfx, cmd.raw());
			});
		});

		gpu.render.probes = probes;

		gpu.render.renderer.light_tile_columns = saved.0;
		gpu.render.renderer.light_tile_rows = saved.1;
	}
}

struct NullProbeGpu;

impl raptor_render::probes::ProbeGpu for NullProbeGpu
{
	fn wait_idle(&mut self) {}

	fn write_volumes(&mut self, _bytes: &[u8]) {}

	fn write_grid(&mut self, _bytes: &[u8]) {}

	fn write_probes(&mut self, _offset: u64, _bytes: &[u8]) {}

	fn write_moments_row(&mut self, _atlas_row: u32, _texels: &[u16]) {}

	fn write_reflection_probes(&mut self, _bytes: &[u8]) {}

	fn write_reflection_cubemap(&mut self, _probe: u32, _halfs: &[u16]) {}

	fn create_capture_resources(&mut self) {}

	fn copy_capture_to_staging(
		&mut self,
		_kind: raptor_render::probes::CaptureKind,
		_slot: u32,
		_face: u32,
	)
	{
	}

	fn read_capture_color(
		&mut self,
		_kind: raptor_render::probes::CaptureKind,
		_slot: u32,
		_face: u32,
	) -> Option<Vec<u16>>
	{
		None
	}

	fn read_capture_depth(&mut self, _slot: u32, _face: u32) -> Option<Vec<f32>>
	{
		None
	}
}
