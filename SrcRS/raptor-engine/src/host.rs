use raptor_asset::scene::SceneHost;
use raptor_core::Key;
use raptor_core::color::Color;
use raptor_core::cvar;
use raptor_entity::CameraCore;
use raptor_game::host::{
	EditorTool, GpuTimes, Host, Input, RagdollSpawn, RenderFlags, SceneStats, Startup, debug_bounds,
};
use raptor_game::ragdoll::{DummyHandle, Impact};
use raptor_gfx::{FrameResult, GpuMarker};
use raptor_input::{WindowEvent, pump_sdl};
use raptor_level::{Collider, ObjectDef};
use raptor_math::Vec3f;
use raptor_render::light::LightKind;
use raptor_render::probe_placement::Plane;
use raptor_render::probe_scene::StoreProbeScene;
use raptor_scene::World;
use raptor_scene::scene::FLAG_UNLIT;

use crate::engine::{Engine, SCENE_DATA_ROOT};

const SCENE_ASPECT_FALLBACK: f32 = 16.0 / 9.0;

pub struct NativeHost
{
	engine: Option<Engine>,
	quit: bool,
}

struct WorldScene<'a>(&'a mut World);

impl SceneHost for WorldScene<'_>
{
	fn is_populated(&self) -> bool
	{
		self.0.populated
	}

	fn set_scene_path(&mut self, path: &str)
	{
		self.0.scene_path = path.to_owned();
	}

	fn set_world_name(&mut self, name: &str)
	{
		self.0.name = name.to_owned();
	}

	fn mark_populated(&mut self)
	{
		self.0.populated = true;
	}

	fn add_collider(&mut self, collider: &Collider)
	{
		self.0.add_collider(collider);
	}

	fn add_object(&mut self, model_path: &str, object: &ObjectDef)
	{
		self.0.request_scene_object(model_path, object);
	}

	fn update_object(&mut self, object: &ObjectDef) -> bool
	{
		self.0.update_object_def(object)
	}
}

impl Default for NativeHost
{
	fn default() -> Self
	{
		Self::new()
	}
}

impl NativeHost
{
	pub fn new() -> Self
	{
		Self {
			engine: None,
			quit: false,
		}
	}

	fn engine(&self) -> &Engine
	{
		self.engine.as_ref().expect("the engine is started")
	}

	fn engine_mut(&mut self) -> &mut Engine
	{
		self.engine.as_mut().expect("the engine is started")
	}

	fn load_scene(engine: &mut Engine, scene: &str)
	{
		let path = format!("{SCENE_DATA_ROOT}/{scene}");

		engine.scene = scene.to_owned();

		if let Err(error) = raptor_asset::scene::load_scene(&mut WorldScene(&mut engine.world), &path) {
			raptor_core::log_error!(Asset; "Could not load the scene '{path}': {error}");
		}
	}

	fn probe_file(engine: &Engine) -> std::path::PathBuf
	{
		let blockout = &engine.world.blockout_path;

		if blockout.is_empty() {
			std::path::Path::new(&engine.world.scene_path).join("probes.fxprobe")
		} else {
			std::path::Path::new(blockout).with_extension("fxprobe")
		}
	}

	fn with_probe_scene<R>(
		engine: &mut Engine,
		run: impl FnOnce(&mut raptor_render::probes::ProbeManager, &StoreProbeScene) -> R,
	) -> R
	{
		let file_path = Self::probe_file(engine);

		let scene = &engine.world.scene;
		let blockout = &engine.world.blockout;

		let is_unlit = |id: u32| scene.core(id).is_some_and(|core| core.has_flag(FLAG_UNLIT));

		let brush_planes = |id: u32| {
			blockout.brush(id).map(|brush| {
				brush
					.planes
					.iter()
					.map(|plane| Plane {
						normal: plane.normal,
						distance: plane.distance,
					})
					.collect::<Vec<_>>()
			})
		};

		let name = |id: u32| scene.name(id).to_owned();

		let probe_scene = StoreProbeScene {
			store: scene.objects(),
			is_unlit: &is_unlit,
			brush_planes: &brush_planes,
			name: &name,
			world_matrix: None,
			file_path,
		};

		run(&mut engine.gpu.render.probes, &probe_scene)
	}

	fn queue_debug_bounds(engine: &mut Engine)
	{
		let mask = engine.world.debug_bounds_mask;

		if mask & debug_bounds::OBJECTS != 0 {
			let color = Color::from_rgba(80, 200, 255, 255).0;

			for id in engine.world.scene.used_ids() {
				if engine.world.scene.is_probe_volume(id) {
					continue;
				}

				if let Some(bounds) = engine.world.scene.world_aabb(id) {
					engine
						.gpu
						.render
						.debug
						.wire_aabb(bounds.min.to_array(), bounds.max.to_array(), color);
				}
			}
		}

		if mask & debug_bounds::LIGHTS != 0 {
			let color = Color::from_rgba(255, 215, 60, 255).0;

			let boxes: Vec<_> = engine
				.gpu
				.render
				.lights
				.iter()
				.filter(|(_, light)| light.kind() != LightKind::Directional)
				.map(|(_, light)| light.bounds())
				.collect();

			for bounds in boxes {
				engine.gpu.render.debug.wire_aabb(bounds.min, bounds.max, color);
			}
		}
	}

	fn set_aspect(camera: &mut CameraCore, aspect: f32)
	{
		camera.aspect = aspect;
		camera.update_projection();
		camera.update_camera_matrix();
	}

	fn flush_texts(engine: &mut Engine)
	{
		let texts = std::mem::take(&mut engine.services.state.borrow_mut().texts);

		if texts.is_empty() {
			return;
		}

		let gfx = std::sync::Arc::clone(&engine.gpu.gfx);
		let pipelines = std::sync::Arc::clone(&engine.gpu.render.pipelines);

		for text in texts {
			engine.gpu.render.text.draw_text_at(
				&gfx,
				&pipelines,
				&text.text,
				text.position,
				text.scale,
				text.color,
			);
		}
	}
}

impl Input for NativeHost
{
	fn poll(&mut self)
	{
		let mut quit = false;

		if let Some(engine) = self.engine.as_mut() {
			let controls = &mut engine.world.controls;

			controls.begin_frame();

			for event in pump_sdl(controls) {
				if event == WindowEvent::Quit {
					quit = true;
				}
			}
		}

		self.quit |= quit;
	}

	fn down(&self, key: Key) -> bool
	{
		self.engine().world.controls.is_down(key)
	}

	fn pressed(&self, key: Key) -> bool
	{
		self.engine().world.controls.is_pressed(key)
	}

	fn mouse_locked(&self) -> bool
	{
		self.engine().world.controls.mouse_captured()
	}

	fn capture_mouse(&mut self)
	{
		let engine = self.engine_mut();
		let window = engine.gpu.gfx.window();

		let position = window.mouse_position();

		window.set_relative_mouse_mode(true);
		engine.world.controls.set_mouse_captured(true, position);
	}

	fn release_mouse(&mut self)
	{
		let engine = self.engine_mut();
		let window = engine.gpu.gfx.window();

		window.set_relative_mouse_mode(false);
		window.warp_mouse(engine.world.controls.captured_mouse_position());
		engine.world.controls.set_mouse_captured(false, (0.0, 0.0));
	}

	fn mouse_delta(&self) -> [f32; 2]
	{
		self.engine().world.controls.mouse_delta()
	}

	fn typed_char(&mut self) -> Option<char>
	{
		self.engine().world.controls.typed_char()
	}

	fn quit_requested(&self) -> bool
	{
		self.quit
	}
}

impl Host for NativeHost
{
	fn startup(&mut self, setup: &Startup) -> Result<(), String>
	{
		let validation = std::env::var_os("RAPTOR_VALIDATION").is_some();

		let mut engine = Engine::create(
			&setup.window_title,
			(setup.window_width, setup.window_height),
			validation,
		)?;

		if let Some(bob) = &setup.head_bob {
			engine.world.player.state.head_bob_strength[0] = bob.scale_x;
			engine.world.player.state.head_bob_strength[1] = bob.scale_y;
		}

		engine.world.init_grid([20, 20]);
		engine.world.create_blockout(setup.blockout.as_deref());

		self.engine = Some(engine);

		Ok(())
	}

	fn begin_game(&mut self, scene: Option<&str>) -> Result<(), String>
	{
		let engine = self.engine_mut();

		let (width, height) = engine.gpu.gfx.window().size();
		let aspect = if height == 0 {
			SCENE_ASPECT_FALLBACK
		} else {
			width as f32 / height as f32
		};

		engine.world.begin_game(aspect);

		if let Some(scene) = scene {
			Self::load_scene(engine, scene);
		}

		Self::with_probe_scene(engine, |probes, scene| {
			probes.load_probes(scene);
		});

		Ok(())
	}

	fn shutdown(&mut self)
	{
		let Some(mut engine) = self.engine.take() else {
			return;
		};

		engine.gpu.gfx.wait_idle();

		engine.gpu.assets.shutdown();
		engine.gpu.meshes.clear();

		drop(engine.world);

		let Engine { gpu, .. } = engine;
		let Gpu { render, gfx, .. } = gpu;

		render.shutdown();

		drop(gfx);

		raptor_gfx::shutdown();
	}

	fn window_focused(&self) -> bool
	{
		self.engine().gpu.gfx.window().is_focused()
	}

	fn window_size(&self) -> [u32; 2]
	{
		let (width, height) = self.engine().gpu.gfx.window().size();

		[width, height]
	}

	fn set_window_title(&mut self, title: &str)
	{
		self.engine().gpu.gfx.window().set_title(title);
	}

	fn did_resize(&self) -> bool
	{
		self.engine().gpu.gfx.did_resize()
	}

	fn update_camera_aspect(&mut self)
	{
		let engine = self.engine_mut();
		let aspect = engine.gpu.gfx.window().aspect_ratio();

		Self::set_aspect(&mut engine.world.player.camera, aspect);
	}

	fn blockout_path(&self) -> String
	{
		self.engine().world.blockout_path.clone()
	}

	fn reload_blockout(&mut self)
	{
		self.engine_mut().world.reload_blockout();
	}

	fn save_blockout(&mut self, path: &str)
	{
		let engine = self.engine_mut();

		engine.drain_services();
		engine.world.save_blockout(path);
	}

	fn reload_scripts(&mut self)
	{
		let engine = self.engine_mut();

		engine
			.services
			.state
			.borrow()
			.scripts
			.lock()
			.unwrap_or_else(std::sync::PoisonError::into_inner)
			.reload_all();

		engine.world.weapons.on_scripts_reloaded();
	}

	fn random_unit(&mut self) -> f32
	{
		raptor_world::random::unit()
	}

	fn editor_active(&self) -> bool
	{
		false
	}

	fn editor_simulation_mode(&self) -> bool
	{
		true
	}

	fn editor_set_tool(&mut self, _tool: EditorTool) {}

	fn editor_update(&mut self, _delta_time: f32) {}

	fn editor_refresh_panels(&mut self) {}

	fn editor_status_lines(&self) -> Vec<String>
	{
		Vec::new()
	}

	fn editor_run_command(&mut self, _name: &str) -> bool
	{
		false
	}

	fn player_position(&self) -> [f32; 3]
	{
		self.engine().world.player.position()
	}

	fn camera_forward(&self) -> [f32; 3]
	{
		self.engine().world.camera_forward()
	}

	fn camera_right(&self) -> [f32; 3]
	{
		self.engine().world.camera_right()
	}

	fn player_move(&mut self, delta_time: f64, movement: [f32; 3])
	{
		self.engine_mut().world.player.move_input(delta_time, movement);
	}

	fn player_update(&mut self, delta_time: f64)
	{
		self.engine_mut().world.update_player(delta_time);
	}

	fn player_rotate_head(&mut self, delta: [f32; 2])
	{
		self.engine_mut().world.player.rotate_head(delta[0], delta[1]);
	}

	fn player_jump(&mut self)
	{
		self.engine_mut().world.player.jump();
	}

	fn player_fly_mode(&self) -> bool
	{
		self.engine().world.player.is_fly_mode()
	}

	fn player_set_fly_mode(&mut self, fly: bool)
	{
		self.engine_mut().world.set_fly_mode(fly);
	}

	fn player_set_sprinting(&mut self, sprinting: bool)
	{
		self.engine_mut().world.set_sprinting(sprinting);
	}

	fn weapons_set_enabled(&mut self, enabled: bool)
	{
		self.engine_mut().world.weapons.set_enabled(enabled);
	}

	fn weapons_set_input(&mut self, flags: u32)
	{
		self.engine_mut().world.weapons_set_input(flags);
	}

	fn weapons_render_hud(&mut self)
	{
		let engine = self.engine_mut();

		engine.world.weapons_render_hud();

		Self::flush_texts(engine);
	}

	fn toggle_render_probes(&mut self) -> bool
	{
		let world = &mut self.engine_mut().world;

		world.render_probes = !world.render_probes;

		world.render_probes
	}

	fn toggle_only_render_probes(&mut self) -> bool
	{
		let state = self.engine().gpu.gfx.state();

		state.set_only_render_probes(!state.only_render_probes());

		state.only_render_probes()
	}

	fn toggle_probe_visibility(&mut self) -> bool
	{
		let state = self.engine().gpu.gfx.state();

		state.set_render_probe_visibility(!state.render_probe_visibility());

		state.render_probe_visibility()
	}

	fn set_debug_bounds_mask(&mut self, mask: u32)
	{
		self.engine_mut().world.debug_bounds_mask = mask;
	}

	fn log_nearby_objects(&mut self)
	{
		raptor_core::log_info!("=== Nearby Objects ===");

		let nearby = self.engine_mut().world.scene.grid.nearby_objects().to_vec();

		for id in nearby {
			raptor_core::log_info!("{id}");
		}
	}

	fn log_player_tile(&self)
	{
		let (xy, _) = self.engine().world.player_tile();

		raptor_core::log_info!("Tile index: {}, {}", xy[0], xy[1]);
	}

	fn probes_rebuild_and_bake(&mut self)
	{
		Self::with_probe_scene(self.engine_mut(), |probes, scene| {
			probes.rebuild_volumes_from_world(scene);
			probes.begin_bake();
		});
	}

	fn probes_save(&mut self)
	{
		Self::with_probe_scene(self.engine_mut(), |probes, scene| {
			probes.save_probes(scene);
		});
	}

	fn probes_service_bake(&mut self)
	{
		self.engine_mut().gpu.render.probes.service_capture_bake();
	}

	fn update_sun(&mut self)
	{
		let engine = self.engine_mut();
		let enabled = cvar::bool("b_sun_enabled", true);

		let player = Vec3f::from_array(engine.world.player.position());

		let render = &mut engine.gpu.render;

		let Some(id) = render.lights.directional() else {
			return;
		};

		let direction = match render.lights.get_mut(id) {
			Some(sun) => {
				sun.set_enabled(enabled);
				sun.position().normalize()
			}
			None => return,
		};

		if enabled {
			render.sun.place_camera(player, direction);
		}
	}

	fn begin_frame(&mut self, delta_time: f32) -> bool
	{
		let engine = self.engine_mut();

		engine.gpu.gfx.state().set_delta_time(delta_time);

		engine.apply_loaded_assets();
		engine.world.pump_models();
		engine.drain_services();

		engine.frame_started = engine.gpu.gfx.begin_frame() == FrameResult::Success;

		engine.frame_started
	}

	fn elapsed_frames(&self) -> u64
	{
		u64::from(self.engine().gpu.gfx.elapsed_frame_count())
	}

	fn physics_update(&mut self)
	{
		self.engine_mut().world.physics_update();
	}

	fn gpu_read_results(&mut self, delta_time: f64)
	{
		self.engine().gpu.gfx.profiler_read_results(delta_time);
	}

	fn gpu_times(&self) -> Option<GpuTimes>
	{
		let gfx = &self.engine().gpu.gfx;

		if !gfx.profiler_enabled() {
			return None;
		}

		Some(GpuTimes {
			total: gfx.gpu_total_ms(),
			shadows: gfx.gpu_ms(GpuMarker::Shadows),
			prepass: gfx.gpu_ms(GpuMarker::Prepass),
			light_culling: gfx.gpu_ms(GpuMarker::LightCulling),
			ssao: gfx.gpu_ms(GpuMarker::Ssao),
			forward: gfx.gpu_ms(GpuMarker::Forward),
			composition: gfx.gpu_ms(GpuMarker::Composition),
			probe_capture: gfx.gpu_ms(GpuMarker::ProbeCapture),
		})
	}

	fn set_render_flags(&mut self, flags: &RenderFlags)
	{
		let state = self.engine().gpu.gfx.state();

		state.set_disable_probes(flags.disable_probes);
		state.set_disable_reflection_probes(flags.disable_reflection_probes);
		state.set_reflection_debug_view(flags.reflection_debug_view);
		state.set_tonemapper(flags.tonemapper);
		state.set_disable_decals(flags.disable_decals);
		state.set_pre_exposure(flags.pre_exposure);
	}

	fn begin_commands(&mut self)
	{
		self.engine().gpu.gfx.begin_commands();
	}

	fn render_world(&mut self)
	{
		let engine = self.engine_mut();

		Self::queue_debug_bounds(engine);

		let shadow_camera = engine.shadow_camera();

		engine
			.frame
			.render(&mut engine.world, &mut engine.gpu, &shadow_camera);

		let camera = engine.world.player.camera.clone();

		engine.frame.draw_debug(&mut engine.gpu, &camera);
	}

	fn compose(&mut self)
	{
		let engine = self.engine_mut();

		Self::flush_texts(engine);

		let gfx = std::sync::Arc::clone(&engine.gpu.gfx);

		engine.gpu.render.renderer.end_geometry(&gfx);

		engine.frame.render_probe_capture(&mut engine.world, &mut engine.gpu);

		if engine.gpu.render.probes.is_capture_pending() || engine.gpu.render.probes.is_baking() {
			gfx.mark_gpu(GpuMarker::ProbeCapture);
		}

		let cmd = gfx.frame_cmd();
		let renderer = &engine.gpu.render.renderer;

		renderer.comp_pass.begin(&gfx, cmd.raw());
		renderer.render_composition(&gfx);
		renderer.comp_pass.end(&gfx, cmd.raw());
		gfx.mark_gpu(GpuMarker::Composition);

		gfx.present_frame();
	}

	fn scene_stats(&self) -> SceneStats
	{
		let engine = self.engine();
		let stats = engine.frame.stats(&engine.gpu);

		SceneStats {
			visible: stats.listed,
			culled: stats.culled,
			tested: stats.tested,
			lights: stats.lights,
			lights_cached: stats.lights_total,
		}
	}

	fn draw_text(&mut self, text: &str, color: u32)
	{
		let engine = self.engine_mut();

		let gfx = std::sync::Arc::clone(&engine.gpu.gfx);
		let pipelines = std::sync::Arc::clone(&engine.gpu.render.pipelines);

		engine.gpu.render.text.draw_text(&gfx, &pipelines, text, 1.0, color);
	}

	fn draw_crosshair(&mut self, position: [f32; 2], size: f32, color: u32)
	{
		let engine = self.engine_mut();

		let Some(image) = engine
			.atlases
			.crosshair
			.as_ref()
			.filter(|handle| handle.is_loaded())
			.map(|handle| handle.image.clone())
		else {
			return;
		};

		let gfx = std::sync::Arc::clone(&engine.gpu.gfx);
		let pipelines = std::sync::Arc::clone(&engine.gpu.render.pipelines);

		engine
			.gpu
			.render
			.text
			.draw_image(&gfx, &pipelines, &image, position, [size, size], color);
	}

	fn ragdoll_request_template(&mut self)
	{
		self.engine_mut().world.request_ragdoll_template();
	}

	fn ragdoll_template_ready(&self) -> bool
	{
		self.engine().world.ragdoll_template_ready()
	}

	fn ragdoll_spawn(
		&mut self,
		position: [f32; 3],
		forward: [f32; 3],
		spawn_index: u32,
	) -> Option<RagdollSpawn>
	{
		self.engine_mut()
			.world
			.ragdoll_spawn(position, forward, spawn_index)
			.map(|(handle, serial)| RagdollSpawn {
				handle: DummyHandle(handle),
				serial,
			})
	}

	fn ragdoll_destroy(&mut self, handle: DummyHandle)
	{
		self.engine_mut().world.ragdoll_destroy(handle.0);
	}

	fn ragdoll_sync(&mut self, handle: DummyHandle, debug_draw: bool)
	{
		let engine = self.engine_mut();

		let boxes = engine.world.ragdoll_sync(handle.0);

		if debug_draw {
			let color = Color::from_rgba(255, 140, 40, 255).0;

			for matrix in boxes {
				let rows = matrix.to_rows();

				let rows = std::array::from_fn(|row| std::array::from_fn(|column| rows[row * 4 + column]));

				engine.gpu.render.debug.wire_box(rows, color);
			}
		}
	}

	fn ragdoll_drain_impacts(&mut self, min_speed: f32) -> Vec<Impact>
	{
		self.engine_mut()
			.world
			.drain_ragdoll_impacts(min_speed)
			.into_iter()
			.map(|impact| Impact {
				serial: u64::from(impact.serial),
				point: impact.point,
				normal: impact.normal,
				speed: impact.speed,
			})
			.collect()
	}

	fn add_blood_splat(&mut self, point: [f32; 3], normal: [f32; 3], size: f32)
	{
		self.engine_mut().gpu.render.decals.add_blood_splat(
			Vec3f::from_array(point),
			Vec3f::from_array(normal),
			size,
		);
	}
}

use crate::gpu::Gpu;
