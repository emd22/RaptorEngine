use std::sync::Arc;

use raptor_core::log_warn;
use raptor_entity::CameraCore;
use raptor_entity::light_core::NO_TILE;
use raptor_gfx::limits::*;
use raptor_gfx::{Gfx, GfxError, Image, imagegen};
use raptor_math::{Frustum, Mat4f};
use raptor_world::WorldGrid;

use crate::debug_renderer::DebugRenderer;
use crate::decal::{AtlasStatus, DecalGpuData, DecalManager, MAX_VISIBLE_DECALS};
use crate::dfg;
use crate::light::{Light, LightId, LightList, LightStore};
use crate::light_gpu::ShadowInputs;
use crate::pipelines::Pipelines;
use crate::probe_gpu::{CaptureHandle, GfxProbeGpu};
use crate::probes::ProbeManager;
use crate::renderer::{Renderer, SceneInputs, ShadowAtlasGpu};
use crate::shadow_atlas::{directional_region, region_uv_transform, spot_tile_region};
use crate::shadow_directional::ShadowDirectional;
use crate::text_renderer::TextRenderer;

pub struct RenderSystem {
	pub gfx: Arc<Gfx>,
	pub pipelines: Arc<Pipelines>,
	pub shadow: ShadowAtlasGpu,
	pub sun: ShadowDirectional,
	pub renderer: Renderer,
	pub text: TextRenderer,
	pub debug: DebugRenderer,
	pub probes: ProbeManager,
	pub capture: CaptureHandle,
	pub decals: DecalManager,
	pub lights: LightStore,
	pub light_list: LightList,
	decal_atlas: Option<Image>,
	decal_normal_atlas: Option<Image>,
	decal_blood_atlas: Option<Image>,
	light_buffer_warned: bool,
}

impl RenderSystem {
	pub fn new(
		gfx: &Arc<Gfx>,
		shader_directory: &str,
		inputs: SceneInputs,
	) -> Result<Self, GfxError> {
		let (probe_gpu, capture) = GfxProbeGpu::new();
		let probes = ProbeManager::new(Box::new(probe_gpu));

		let shadow = ShadowAtlasGpu::new(gfx);
		let sun = ShadowDirectional::new();

		let noise = imagegen::random_noise(gfx, (64, 64), 0x5EED_1234_ABCD)?;
		gfx.set_noise_texture(noise);

		let mut lut = vec![0u16; (DFG_LUT_SIZE * DFG_LUT_SIZE * 2) as usize];
		dfg::generate_lut(DFG_LUT_SIZE, &mut lut);
		gfx.set_dfg_lut(imagegen::from_rg16(gfx, DFG_LUT_SIZE, &lut)?);

		let pipelines = Arc::new(Pipelines::new(shader_directory));

		let text = TextRenderer::new(gfx);

		let renderer = Renderer::new(
			gfx,
			Arc::clone(&pipelines),
			inputs,
			&shadow,
			Arc::clone(text.instance_buffer().record()),
		);

		let stages = renderer.stages();

		gfx.set_resize_hook(move |gfx| {
			for stage in &stages {
				stage.rebuild(gfx, None);
			}
		});

		Ok(Self {
			gfx: Arc::clone(gfx),
			pipelines,
			shadow,
			sun,
			renderer,
			text,
			debug: DebugRenderer::new(),
			probes,
			capture,
			decals: DecalManager::new(),
			lights: LightStore::new(MAX_ACTIVE_LIGHTS as usize),
			light_list: LightList::default(),
			decal_atlas: None,
			decal_normal_atlas: None,
			decal_blood_atlas: None,
			light_buffer_warned: false,
		})
	}

	pub fn add_light(&mut self, light: Light, grid: &mut WorldGrid) -> Option<LightId> {
		self.lights.add(light, grid)
	}

	pub fn destroy_light(&mut self, id: LightId, grid: &mut WorldGrid) -> LightId {
		self.lights
			.destroy(id, grid, &mut self.light_list, &mut self.shadow.state)
	}

	pub fn set_decal_atlases(
		&mut self,
		atlas: Option<Image>,
		normal_atlas: Option<Image>,
		blood_atlas: Option<Image>,
	) {
		self.decal_atlas = atlas;
		self.decal_normal_atlas = normal_atlas;
		self.decal_blood_atlas = blood_atlas;
	}

	pub fn update_decals(&mut self, camera: &CameraCore) {
		let gfx = &self.gfx;

		self.renderer.set_decal_atlases(
			gfx,
			&self.shadow,
			self.decal_atlas.as_ref(),
			self.decal_normal_atlas.as_ref(),
			self.decal_blood_atlas.as_ref(),
		);

		let buffers = gfx.buffers();
		let page_offset = buffers.decal_frame_offset(gfx.frame_number()) as usize;

		let mapped = buffers.decal.mapped_ptr();

		if mapped.is_null() {
			return;
		}

		let frustum = Frustum::from_view_projection(&Mat4f::from_rows(&camera.camera_matrix));

		// SAFETY: the decal buffer is persistently mapped with a page of `MAX_VISIBLE_DECALS`
		// decals for each frame in flight, and nothing else touches this frame's page now.
		let page = unsafe {
			std::slice::from_raw_parts_mut(
				mapped.add(page_offset).cast::<DecalGpuData>(),
				MAX_VISIBLE_DECALS,
			)
		};

		self.decals.update(
			&frustum,
			AtlasStatus {
				atlas: self.decal_atlas.is_some(),
				normal_atlas: self.decal_normal_atlas.is_some(),
				blood_atlas: self.decal_blood_atlas.is_some(),
			},
			page,
		);

		let count = self.decals.visible_count();

		if count > 0 {
			let _ = buffers.decal.flush(
				page_offset as u64,
				u64::from(count) * size_of::<DecalGpuData>() as u64,
			);
		}
	}

	pub fn submit_light(
		&mut self,
		id: LightId,
		camera: &CameraCore,
		shadow_camera: Option<&CameraCore>,
	) {
		let gfx = Arc::clone(&self.gfx);

		let mut uniforms = gfx
			.buffers()
			.light
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		let Some(light) = self.lights.get_mut(id) else {
			return;
		};

		if !light.is_enabled() {
			return;
		}

		if uniforms.slot_index() >= uniforms.capacity() {
			if !std::mem::replace(&mut self.light_buffer_warned, true) {
				log_warn!(
					Render;
					"Light buffer is full ({} lights), extra lights will not be rendered",
					uniforms.capacity()
				);
			}

			return;
		}

		let light_camera = shadow_camera.unwrap_or(camera);

		let mut inputs = ShadowInputs {
			directional_matrix: light_camera.camera_matrix,
			directional_rect: [0.0; 4],
			spot_rect: None,
		};

		match light.kind() {
			crate::light::LightKind::Directional => {
				inputs.directional_rect = region_uv_transform(directional_region());
			}
			crate::light::LightKind::Spot => {
				if light.core.shadow_atlas_tile != NO_TILE
					&& let Some(region) = spot_tile_region(light.core.shadow_atlas_tile)
				{
					inputs.spot_rect = Some(region_uv_transform(region));
				}
			}
			crate::light::LightKind::Point => {}
		}

		let data = light.gpu_data(&inputs);
		let frame = gfx.frame_number();

		uniforms.write(frame, &data);
		uniforms.flush_to_gpu(frame);
		uniforms.next_slot();
	}

	pub fn submit_lights(&mut self, camera: &CameraCore, shadow_camera: Option<&CameraCore>) {
		let ids: Vec<_> = self.light_list.valid().collect();

		for id in ids {
			self.submit_light(id, camera, shadow_camera);
		}
	}

	pub fn shutdown(self) {
		self.gfx.wait_idle();

		self.renderer.release(&self.gfx);
		self.shadow.stage.release(&self.gfx);

		if let Some(guard) = self.capture.resources().take() {
			guard.capture_stage.release(&self.gfx);
			guard.reflection_stage.release(&self.gfx);
		}

		if let Ok(pipelines) = Arc::try_unwrap(self.pipelines) {
			pipelines.destroy(&self.gfx);
		}
	}
}
