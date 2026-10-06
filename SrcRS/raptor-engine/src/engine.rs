use std::path::PathBuf;
use std::rc::Rc;
use std::sync::{Arc, Mutex};

use raptor_asset::manager::{AssetManager, ImageHandle, ImageRequest};
use raptor_asset::material_library::MaterialLibrary;
use raptor_asset::materials::MaterialStore;
use raptor_core::color::Color;
use raptor_core::cvar;
use raptor_entity::CameraCore;
use raptor_gfx::{Gfx, GfxConfig, RendererBufferSizes, Window};
use raptor_gpu::ImageFormat;
use raptor_jolt::JoltBackend;
use raptor_level::blockout::{Level, LightKind as LevelLightKind};
use raptor_math::Vec3f;
use raptor_render::decal::{
	BLOOD_ATLAS_PATH, BULLET_HOLE_ATLAS_PATH, BULLET_HOLE_NORMAL_ATLAS_PATH, DecalGpuData,
};
use raptor_render::light::{Light, LightKind};
use raptor_render::light_gpu::LightGpuData;
use raptor_render::pipeline_desc::PipelineDesc;
use raptor_render::probe_data::VolumeData;
use raptor_render::probes::{ProbeInfo, ReflectionProbeData};
use raptor_render::render_system::RenderSystem;
use raptor_render::renderer::SceneInputs;
use raptor_scene::World;
use raptor_script::ScriptManager;
use raptor_world::WorldGrid;

use crate::frame::WorldRenderer;
use crate::gpu::Gpu;
use crate::meshes::MeshStore;
use crate::object_buffer::{self, ObjectBuffer};
use crate::services::{EngineServices, State};

pub const TEXT_ATLAS_PATH: &str = "Textures/debug_font3.png";
pub const CROSSHAIR_PATH: &str = "Textures/crosshair.png";
pub const SCENE_DATA_ROOT: &str = "RaptorData/Data";

pub struct Atlases
{
	pub text: Option<ImageHandle>,
	pub crosshair: Option<ImageHandle>,
	pub decal: Option<ImageHandle>,
	pub decal_normal: Option<ImageHandle>,
	pub blood: Option<ImageHandle>,
	pub text_applied: bool,
	pub decals_applied: bool,
}

pub struct Engine
{
	pub world: World<JoltBackend>,
	pub gpu: Gpu,
	pub services: EngineServices,
	pub frame: WorldRenderer,
	pub atlases: Atlases,
	pub scene: String,
	pub frame_started: bool,
	pub only_render_probes: bool,
	pub render_probe_visibility: bool,
}

fn declare_material(gfx: &Arc<Gfx>, assets: &Arc<AssetManager>) -> Box<dyn Fn(&mut PipelineDesc)>
{
	let gfx = Arc::clone(gfx);
	let assets = Arc::clone(assets);

	Box::new(move |desc: &mut PipelineDesc| {
		use ash::vk::ShaderStageFlags as Stage;

		let null = assets.null_image(ImageFormat::Rgba8UNorm);
		let sampler = gfx.sampler(&raptor_gpu::SamplerProps::default());

		for binding in 0..3 {
			desc.add_image(1, binding, Stage::FRAGMENT, null.image.record(), sampler);
		}

		let buffers = gfx.buffers();

		{
			let bone = buffers
				.bone
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner());
			desc.add_buffer(
				1,
				3,
				Stage::VERTEX,
				bone.gpu_buffer().record(),
				0,
				u64::from(bone.page_size()),
			);
		}

		{
			let light = buffers
				.light
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner());
			desc.add_buffer(
				1,
				4,
				Stage::FRAGMENT,
				light.gpu_buffer().record(),
				0,
				u64::from(light.page_size()),
			);
		}
	})
}

fn load_image(assets: &AssetManager, path: &str) -> ImageHandle
{
	assets.load_image(
		raptor_asset::fs::resolve_path(path),
		ImageRequest::flat(ImageFormat::Rgba8UNorm),
	)
}

impl Engine
{
	pub fn create(title: &str, size: (u32, u32), validation: bool) -> Result<Self, String>
	{
		let window = Arc::new(Window::new(title, size).map_err(|error| error.to_string())?);

		let config = GfxConfig {
			app_name: title.to_owned(),
			validation,
			buffer_sizes: RendererBufferSizes {
				light_slot: size_of::<LightGpuData>() as u32,
				decal: size_of::<DecalGpuData>() as u32,
				probe_sh: size_of::<ProbeInfo>() as u32,
				probe_volume: size_of::<VolumeData>() as u32,
				reflection_probe: size_of::<ReflectionProbeData>() as u32,
			},
		};

		let gfx =
			raptor_gfx::install(Gfx::create(window, &config).map_err(|error| error.to_string())?);

		let assets = Arc::new(AssetManager::new(
			Arc::clone(gfx.upload()),
			Arc::clone(gfx.textures()),
			3,
		));

		let materials =
			Arc::new(MaterialStore::new(&gfx, &assets).map_err(|error| format!("{error:?}"))?);
		let meshes = Arc::new(MeshStore::new(Arc::clone(gfx.core())));
		let objects = ObjectBuffer::new(gfx.core()).map_err(|error| format!("{error:?}"))?;

		let inputs = SceneInputs {
			object_buffer: Arc::clone(objects.buffer().record()),
			object_bound_size: u64::from(object_buffer::PAGE_SIZE),
			object_page_size: u64::from(object_buffer::PAGE_SIZE),
			material_buffer: Arc::clone(materials.properties_buffer().record()),
			material_buffer_size: materials.properties_buffer().size(),
			null_rgba8: Arc::clone(assets.null_image(ImageFormat::Rgba8UNorm).image.record()),
			null_rg16: Arc::clone(assets.null_image(ImageFormat::Rg16UNorm).image.record()),
			null_depth: Arc::clone(assets.null_image(ImageFormat::D32Float).image.record()),
			declare_material: declare_material(&gfx, &assets),
		};

		let shader_directory =
			raptor_core::paths::asset_path(raptor_core::paths::PathQuery::Shaders)
				.to_string_lossy()
				.into_owned()
				+ "/";

		let mut render = RenderSystem::new(&gfx, &shader_directory, inputs)
			.map_err(|error| error.to_string())?;

		render.sun.camera.set_planes(0.1, 400.0);
		render.sun.camera.update_projection();
		render.sun.camera.update_camera_matrix();

		let scripts = Arc::new(Mutex::new(ScriptManager::new()));

		raptor_weapon::strata::register_natives();

		let base = raptor_core::paths::base_dir().clone();

		let services = EngineServices::new(State {
			gfx: Arc::clone(&gfx),
			assets: Arc::clone(&assets),
			materials: Arc::clone(&materials),
			meshes: Arc::clone(&meshes),
			scripts,
			registry: raptor_level::MaterialRegistry::new(),
			selection_material: 0,
			requests: Default::default(),
			next_ticket: 0,
			decals: Vec::new(),
			texts: Vec::new(),
			levels: Vec::new(),
			environment: Default::default(),
			detached: Vec::new(),
			base: base.clone(),
		});

		let backend = JoltBackend::new().ok_or("Jolt could not be initialised")?;

		let world = World::new(backend, Box::new(services.clone()), base);

		let atlases = Atlases {
			text: Some(load_image(&assets, TEXT_ATLAS_PATH)),
			crosshair: Some(load_image(&assets, CROSSHAIR_PATH)),
			decal: Some(load_image(&assets, BULLET_HOLE_ATLAS_PATH)),
			decal_normal: Some(load_image(&assets, BULLET_HOLE_NORMAL_ATLAS_PATH)),
			blood: Some(load_image(&assets, BLOOD_ATLAS_PATH)),
			text_applied: false,
			decals_applied: false,
		};

		Ok(Self {
			world,
			gpu: Gpu {
				gfx,
				render,
				assets,
				materials,
				meshes,
				objects,
				library: MaterialLibrary::new(),
			},
			services,
			frame: WorldRenderer::new(),
			atlases,
			scene: String::new(),
			frame_started: false,
			only_render_probes: false,
			render_probe_visibility: false,
		})
	}

	pub fn base_path(&self) -> PathBuf
	{
		self.world.base().to_path_buf()
	}

	pub fn shadow_camera(&self) -> CameraCore
	{
		self.gpu.render.sun.camera.clone()
	}

	pub fn apply_loaded_assets(&mut self)
	{
		let atlases = &mut self.atlases;
		let render = &mut self.gpu.render;

		if !atlases.text_applied
			&& let Some(text) = &atlases.text
			&& text.is_loaded()
		{
			render.text.set_atlas(&self.gpu.gfx, text.image.clone());
			atlases.text_applied = true;
		}

		if !atlases.decals_applied {
			let ready = [&atlases.decal, &atlases.decal_normal, &atlases.blood]
				.iter()
				.all(|handle| handle.as_ref().is_some_and(ImageHandle::is_loaded));

			if ready {
				render.set_decal_atlases(
					atlases.decal.as_ref().map(|handle| handle.image.clone()),
					atlases
						.decal_normal
						.as_ref()
						.map(|handle| handle.image.clone()),
					atlases.blood.as_ref().map(|handle| handle.image.clone()),
				);
				atlases.decals_applied = true;
			}
		}
	}

	fn apply_color(light: &mut Light, color: Option<[i32; 3]>, intensity: Option<f32>)
	{
		let current = Color(light.core.color);

		let (r, g, b) = color.map_or((current.r(), current.g(), current.b()), |color| {
			(
				color[0].clamp(0, 255) as u8,
				color[1].clamp(0, 255) as u8,
				color[2].clamp(0, 255) as u8,
			)
		});

		light.core.color = Color::from_rgba(r, g, b, 255).0;

		if let Some(intensity) = intensity {
			light.core.intensity = intensity;
		}
	}

	fn apply_level_lights(render: &mut RenderSystem, grid: &mut WorldGrid, level: &Level)
	{
		let sun_id = render
			.lights
			.directional()
			.or_else(|| render.lights.add(Light::directional("Sun"), grid));

		let sun_enabled = level.sun.as_ref().is_some_and(|sun| sun.enabled);

		if let Some(sun) = sun_id.and_then(|id| render.lights.get_mut(id)) {
			sun.set_enabled(sun_enabled);

			if let Some(entry) = &level.sun {
				sun.set_position(Vec3f::from_array(entry.position), grid);
				Self::apply_color(sun, entry.color, entry.intensity);
			}
		}

		let _ = cvar::set_bool("b_sun_enabled", sun_enabled);

		let mut names = Vec::new();

		for entry in &level.lights {
			let kind = match entry.kind {
				LevelLightKind::Point => LightKind::Point,
				LevelLightKind::Spot => LightKind::Spot,
				LevelLightKind::Unknown(kind) => {
					raptor_core::log_error!(Asset; "Light '{}' has unknown type {kind}", entry.name);
					continue;
				}
			};

			names.push(entry.name.clone());

			let id = match render.lights.find(&entry.name) {
				Some(id) => {
					if render
						.lights
						.get(id)
						.is_some_and(|light| light.kind() != kind)
					{
						raptor_core::log_warn!(Asset; "Light '{}' changed type, restart to apply", entry.name);
						continue;
					}

					id
				}
				None => {
					let Some(id) = render.lights.add(Light::new(&entry.name, kind), grid) else {
						continue;
					};

					id
				}
			};

			let Some(light) = render.lights.get_mut(id) else {
				continue;
			};

			if let Some(position) = entry.position {
				light.set_position(Vec3f::from_array(position), grid);
			}

			Self::apply_color(light, entry.color, entry.intensity);
			light.set_radius(entry.radius, grid);

			if kind == LightKind::Spot {
				if let Some(direction) = entry.direction {
					light.set_direction(Vec3f::from_array(direction), grid);
				} else if let Some(rotation) = entry.rotation {
					light.set_rotation(rotation, grid);
				}

				light.set_cone_angles(
					entry.inner_degrees.to_radians(),
					entry.outer_degrees.to_radians(),
					grid,
				);
				light.set_cast_shadows(entry.shadows);
			}
		}

		let stale: Vec<_> = render
			.lights
			.iter()
			.filter(|(_, light)| {
				matches!(light.kind(), LightKind::Point | LightKind::Spot)
					&& !names.contains(&light.name)
			})
			.map(|(id, _)| id)
			.collect();

		for id in stale {
			render.destroy_light(id, grid);
		}
	}

	fn apply_level_camera(level: &Level)
	{
		let Some(camera) = &level.camera else {
			return;
		};

		let apply = |name: &str, value: Option<f32>, fallback: f32| {
			let _ = cvar::set_float(name, value.unwrap_or_else(|| cvar::float(name, fallback)));
		};

		apply("r_aperture", camera.aperture, 16.0);
		apply("r_shutter", camera.shutter, 0.01);
		apply("r_iso", camera.iso, 100.0);
		apply("r_exposure_ev", camera.exposure_ev, 0.0);
	}

	pub fn drain_services(&mut self)
	{
		let (levels, decals, detached) = {
			let mut state = self.services.state.borrow_mut();

			(
				std::mem::take(&mut state.levels),
				std::mem::take(&mut state.decals),
				std::mem::take(&mut state.detached),
			)
		};

		for level in &levels {
			Self::apply_level_lights(&mut self.gpu.render, &mut self.world.scene.grid, level);
			Self::apply_level_camera(level);
		}

		for object in detached {
			self.frame.invalidate_object(object);
		}

		for decal in decals {
			let point = Vec3f::from_array(decal.point);
			let normal = Vec3f::from_array(decal.normal);

			match decal.size {
				Some(size) => self.gpu.render.decals.add_blood_splat(point, normal, size),
				None => self.gpu.render.decals.add_bullet_hole(point, normal),
			}
		}

		self.capture_environment();
	}

	fn capture_environment(&mut self)
	{
		use raptor_level::blockout::{CameraOut, LightOut, SunOut};

		let render = &self.gpu.render;

		let pack = |color: u32| {
			let color = Color(color);
			[
				i32::from(color.r()),
				i32::from(color.g()),
				i32::from(color.b()),
				i32::from(color.a()),
			]
		};

		let sun = render
			.lights
			.directional()
			.and_then(|id| render.lights.get(id))
			.map(|light| SunOut {
				enabled: light.is_enabled(),
				position: light.position().to_array(),
				color: pack(light.core.color),
				intensity: light.core.intensity,
			});

		let lights: Vec<LightOut> = render
			.lights
			.iter()
			.filter(|(_, light)| matches!(light.kind(), LightKind::Point | LightKind::Spot))
			.map(|(_, light)| LightOut {
				name: light.name.clone(),
				spot: light.kind() == LightKind::Spot,
				position: light.position().to_array(),
				color: pack(light.core.color),
				intensity: light.core.intensity,
				radius: light.radius(),
				direction: light.direction().to_array(),
				inner_degrees: light.core.inner_angle.to_degrees(),
				outer_degrees: light.core.outer_angle.to_degrees(),
				shadows: light.casts_shadows(),
			})
			.collect();

		let camera = CameraOut {
			aperture: cvar::float("r_aperture", 16.0),
			shutter: cvar::float("r_shutter", 0.01),
			iso: cvar::float("r_iso", 100.0),
			exposure_ev: cvar::float("r_exposure_ev", 0.0),
		};

		let mut state = self.services.state.borrow_mut();

		state.environment.sun = sun;
		state.environment.lights = lights;
		state.environment.camera = Some(camera);
	}

	pub fn services_rc(&self) -> Rc<std::cell::RefCell<State>>
	{
		Rc::clone(&self.services.state)
	}
}
