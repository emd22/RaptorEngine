use std::time::{Duration, Instant};

use raptor_core::console::{CommandHost, Console};
use raptor_core::{Color, Key, cvar, log_info, log_warn};

use crate::blood;
use crate::config::GameConfig;
use crate::exposure::ExposureSettings;
use crate::fps::FpsMeter;
use crate::host::{EditorTool, Host, RenderFlags, Startup, debug_bounds, weapon_input};
use crate::ragdoll::DummySet;
use crate::title::TitleTracker;

const MOUSE_RADIANS_PER_PIXEL: f32 = 0.25 / 120.0;
const UNFOCUSED_FRAME_TIME: f64 = 1.0 / 10.0;
const DEFAULT_CROSSHAIR_SIZE: i64 = 3;
const DEFAULT_BLOCKOUT_SAVE_PATH: &str = "RaptorData/Data/blockouts/btemp.prx";
const WHITE: u32 = Color::WHITE.0;
const GREEN: u32 = Color::from_rgba(100, 255, 0, 255).0;

pub struct Game {
	config: GameConfig,
	console: Console,
	fps: FpsMeter,
	title: TitleTracker,
	dummies: DummySet,
	drops_pending: u32,
	template_requested: bool,
	in_command_mode: bool,
	running: bool,
	delta_time: f64,
}

struct EditorCommands<'a, H: Host>(&'a mut H);

impl<H: Host> CommandHost for EditorCommands<'_, H> {
	fn run_command(&mut self, name: &str) -> bool {
		self.0.editor_run_command(name)
	}
}

impl Game {
	pub fn new(config: GameConfig) -> Self {
		Self {
			config,
			console: Console::new(),
			fps: FpsMeter::default(),
			title: TitleTracker::default(),
			dummies: DummySet::default(),
			drops_pending: 0,
			template_requested: false,
			in_command_mode: false,
			running: true,
			delta_time: 1.0 / 60.0,
		}
	}

	pub fn run<H: Host>(config: GameConfig, host: &mut H, args: Vec<String>) -> Result<(), String> {
		let mut game = Self::new(config);

		game.init(host, args)?;

		game.create_game(host)?;

		game.main_loop(host);

		game.destroy(host);

		Ok(())
	}

	pub fn delta_time(&self) -> f64 {
		self.delta_time
	}

	pub fn is_running(&self) -> bool {
		self.running
	}

	pub fn in_command_mode(&self) -> bool {
		self.in_command_mode
	}

	pub fn init<H: Host>(&mut self, host: &mut H, args: Vec<String>) -> Result<(), String> {
		host.startup(&Startup {
			window_title: self.config.window_title.clone(),
			window_width: self.config.window_width,
			window_height: self.config.window_height,
			head_bob: self.config.head_bob.clone(),
			blockout: self.config.blockout.clone(),
			scene: self.config.scene.clone(),
			args,
		})?;

		if let Some(bob) = &self.config.head_bob {
			let _ = cvar::set_bool("b_headbob_enabled", bob.enabled);
		}

		Ok(())
	}

	fn init_cvars() {
		let keep_float = |name: &str, fallback: f32| {
			let _ = cvar::set_float(name, cvar::float(name, fallback));
		};

		let _ = cvar::set_int("i_crosshair_size", DEFAULT_CROSSHAIR_SIZE);
		let _ = cvar::set_int("i_show_fps", 1);
		let _ = cvar::set_int("i_show_gpu", 1);
		let _ = cvar::set_int("r_probes", 1);
		let _ = cvar::set_int("r_decals", 1);
		let _ = cvar::set_int("r_debug_bounds", 0);

		keep_float("r_exposure_ev", 0.0);
		keep_float("r_aperture", 16.0);
		keep_float("r_shutter", 0.01);
		keep_float("r_iso", 100.0);

		let _ = cvar::set_int("r_tonemapper", 1);
		let _ = cvar::set_float("r_probe_spacing", 2.5);
		let _ = cvar::set_float("r_probe_level_spacing", 0.5);
		let _ = cvar::set_int("r_probe_bounces", 3);
		let _ = cvar::set_int("r_reflection_probes", 1);
		let _ = cvar::set_int("r_reflection_level_probe", 0);
		let _ = cvar::set_int("r_reflection_debug", 0);

		keep_float("b_ragdoll_blood_speed", blood::DEFAULT_MIN_SPEED);
		keep_float("r_ssao_radius", 0.25);
		keep_float("r_ssao_bias", 0.02);
		keep_float("r_ssao_strength", 1.5);
		keep_float("r_ssao_power", 1.2);
		keep_float("r_ssao_floor", 0.35);
	}

	pub fn create_game<H: Host>(&mut self, host: &mut H) -> Result<(), String> {
		Self::init_cvars();

		host.begin_game(self.config.scene.as_deref())?;

		if host.editor_active() {
			host.editor_set_tool(EditorTool::Translate);
		}

		Ok(())
	}

	pub fn main_loop<H: Host>(&mut self, host: &mut H) {
		let mut last_tick = Instant::now();

		while self.running {
			let frame_start = Instant::now();

			self.tick(host, &mut last_tick);

			if !host.window_focused() {
				let remaining = UNFOCUSED_FRAME_TIME - frame_start.elapsed().as_secs_f64();

				if remaining > 0.0 {
					std::thread::sleep(Duration::from_secs_f64(remaining));
				}
			}
		}
	}

	fn movement_vector<H: Host>(host: &H) -> [f32; 3] {
		let axis = |positive: Key, negative: Key| {
			f32::from(u8::from(host.down(positive))) - f32::from(u8::from(host.down(negative)))
		};

		[
			axis(Key::KeyD, Key::KeyA),
			axis(Key::KeyE, Key::KeyQ),
			axis(Key::KeyW, Key::KeyS),
		]
	}

	fn toggle_editor_mode<H: Host>(host: &mut H) {
		if !host.editor_active() {
			return;
		}

		host.editor_set_tool(if host.editor_simulation_mode() {
			EditorTool::Translate
		} else {
			EditorTool::None
		});
	}

	fn reload_scripts<H: Host>(host: &mut H) {
		log_info!("Reloading all scripts...");

		host.reload_scripts();
	}

	pub fn process_controls<H: Host>(&mut self, host: &mut H) {
		let is_simulation_mode = !host.editor_active() || host.editor_simulation_mode();

		let was_mouse_locked = host.mouse_locked();

		if host.combo_pressed(Key::KeyLshift, Key::KeyGrave) {
			host.release_mouse();
			self.running = false;
		}

		if host.pressed(Key::MouseLeft) && !host.mouse_locked() {
			host.capture_mouse();
		} else if host.pressed(Key::KeyEscape) && host.mouse_locked() {
			host.release_mouse();
		}

		if host.pressed(Key::KeyF8) {
			self.request_ragdoll_drop(host);
		}

		if host.pressed(Key::KeyL) {
			let enabled = host.toggle_render_probes();

			log_info!(
				"Probe debug render {}",
				if enabled { "enabled" } else { "disabled" }
			);
		}

		let bounds_keys = [
			(Key::KeyF5, debug_bounds::OBJECTS),
			(Key::KeyF6, debug_bounds::LIGHTS),
			(Key::KeyF7, debug_bounds::PHYSICS),
		];

		for (key, bit) in bounds_keys {
			if host.pressed(key) {
				let mask = (cvar::int("r_debug_bounds", 0) as u32) ^ bit;

				let _ = cvar::set_int("r_debug_bounds", i64::from(mask));

				let state = |bit: u32| if mask & bit != 0 { "on" } else { "off" };

				log_info!(
					"Debug bounds: objects {}, lights {}, physics {}",
					state(debug_bounds::OBJECTS),
					state(debug_bounds::LIGHTS),
					state(debug_bounds::PHYSICS)
				);
			}
		}

		if host.pressed(Key::Key2) {
			let enabled = host.toggle_only_render_probes();

			log_info!(
				"Probe irradiance debug view {}",
				if enabled { "enabled" } else { "disabled" }
			);
		}

		if host.pressed(Key::Key3) {
			let enabled = host.toggle_probe_visibility();

			log_info!(
				"Probe visibility debug view {}",
				if enabled { "enabled" } else { "disabled" }
			);
		}

		if host.pressed(Key::Key4) {
			const VIEW_NAMES: [&str; 3] = ["off", "mirror reflections", "probe coverage"];

			let view = (cvar::int("r_reflection_debug", 0).clamp(0, 2) + 1) % 3;

			let _ = cvar::set_int("r_reflection_debug", view);

			log_info!("Reflection probe debug view: {}", VIEW_NAMES[view as usize]);
		}

		if host.mouse_locked() {
			let delta = host.mouse_delta();

			host.player_rotate_head([
				delta[0] * MOUSE_RADIANS_PER_PIXEL,
				delta[1] * -MOUSE_RADIANS_PER_PIXEL,
			]);
		}

		if host.down(Key::KeySpace) && !host.player_fly_mode() {
			host.player_jump();
		}

		if host.pressed(Key::KeyX) {
			Self::toggle_editor_mode(host);
		}

		let sprinting = host.down(Key::KeyLshift);

		host.player_set_sprinting(sprinting);

		host.weapons_set_enabled(is_simulation_mode);

		if is_simulation_mode {
			let mut flags = 0;

			if was_mouse_locked && host.pressed(Key::MouseLeft) {
				flags |= weapon_input::FIRE_PRESSED;
			}

			if host.mouse_locked() && host.down(Key::MouseLeft) {
				flags |= weapon_input::FIRE_HELD;
			}

			if host.pressed(Key::KeyR) && !host.down(Key::KeyLshift) {
				flags |= weapon_input::RELOAD_PRESSED;
			}

			if host.pressed(Key::KeyV) {
				flags |= weapon_input::SWITCH_MODE;
			}

			if host.pressed(Key::KeyTab) {
				flags |= weapon_input::SWITCH_WEAPON;
			}

			host.weapons_set_input(flags);
		}

		if host.combo_pressed(Key::KeyLshift, Key::KeyR) {
			log_info!("Reloading blockout...");
			host.reload_blockout();
		}

		if host.pressed(Key::Key0) {
			Self::reload_scripts(host);
			host.log_player_tile();
		}

		if host.pressed(Key::KeyH) {
			host.log_nearby_objects();
		}

		if host.pressed(Key::KeyN) {
			let fly = !host.player_fly_mode();

			host.player_set_fly_mode(fly);
		}

		if host.pressed(Key::KeyG) {
			host.probes_rebuild_and_bake();
		}

		if host.pressed(Key::KeyP) {
			host.probes_save();
		}

		if host.pressed(Key::KeySlash) {
			self.in_command_mode = !self.in_command_mode;
		}

		if host.combo_pressed(Key::KeyLmeta, Key::KeyS) && !host.down(Key::KeyLshift) {
			let current = host.blockout_path();

			let path = if current.is_empty() {
				DEFAULT_BLOCKOUT_SAVE_PATH.to_owned()
			} else {
				current
			};

			log_info!("Saving blockout to '{path}'");
			host.save_blockout(&path);
		}
	}

	fn handle_console_keyboard<H: Host>(&mut self, host: &mut H) {
		if host.pressed(Key::KeyBackspace) {
			let clear_line = host.down(Key::KeyLmeta);

			self.console.backspace(clear_line);
		}

		if let Some(ch) = host.typed_char() {
			self.console.push_char(ch, &mut EditorCommands(host));
		}
	}

	pub fn request_ragdoll_drop<H: Host>(&mut self, host: &mut H) {
		self.drops_pending += 1;

		if self.template_requested {
			return;
		}

		self.template_requested = true;

		host.ragdoll_request_template();

		log_info!("Loading the ragdoll model, dummies drop once it has loaded");
	}

	fn destroy_dummy<H: Host>(host: &mut H, handle: crate::ragdoll::DummyHandle) {
		host.ragdoll_destroy(handle);
	}

	fn drop_ragdoll_dummy<H: Host>(&mut self, host: &mut H) {
		if self.dummies.needs_eviction()
			&& let Some(oldest) = self.dummies.evict_oldest()
		{
			Self::destroy_dummy(host, oldest.handle);
		}

		let placement = self.dummies.placement(
			host.player_position(),
			host.camera_forward(),
			host.camera_right(),
		);

		match host.ragdoll_spawn(
			placement.position,
			placement.forward,
			self.dummies.spawn_count() - 1,
		) {
			Some(spawn) => self.dummies.add(spawn.handle, spawn.serial),
			None => log_warn!("The ragdoll model could not be dropped"),
		}
	}

	pub fn update_ragdoll_dummies<H: Host>(&mut self, host: &mut H) {
		while self.drops_pending > 0 && host.ragdoll_template_ready() {
			self.drops_pending -= 1;
			self.drop_ragdoll_dummy(host);
		}

		if self.dummies.is_empty() {
			return;
		}

		let draw_debug = (cvar::int("r_debug_bounds", 0) as u32) & debug_bounds::PHYSICS != 0;
		let min_speed = cvar::float("b_ragdoll_blood_speed", blood::DEFAULT_MIN_SPEED);

		self.dummies.tick_cooldowns(self.delta_time as f32);

		for impact in host.ragdoll_drain_impacts(min_speed) {
			if !self.dummies.take_blood_slot(impact.serial) {
				continue;
			}

			let splats =
				blood::splats(impact.point, impact.normal, impact.speed, min_speed, || {
					host.random_unit()
				});

			for splat in splats {
				host.add_blood_splat(splat.point, impact.normal, splat.size);
			}
		}

		for dummy in self.dummies.iter() {
			host.ragdoll_sync(dummy.handle, draw_debug);
		}
	}

	fn exposure() -> f32 {
		ExposureSettings {
			compensation: cvar::float("r_exposure_ev", 0.0),
			aperture: cvar::float("r_aperture", 16.0),
			shutter_time: cvar::float("r_shutter", 0.01),
			iso: cvar::float("r_iso", 100.0),
		}
		.exposure()
	}

	fn render_text<H: Host>(&self, host: &mut H) {
		if self.in_command_mode {
			host.draw_text(&format!(":{}", self.console.entry()), GREEN);
			host.draw_text(&format!("={}", self.console.output), WHITE);
			return;
		}

		if cvar::int("i_show_fps", 1) != 0 {
			host.draw_text(
				&format!("FPS={:.0} ({:.2}ms)", self.fps.fps, self.fps.frame_time_ms),
				WHITE,
			);
		}

		if let Some(gpu) = host.gpu_times().filter(|_| cvar::int("i_show_gpu", 1) != 0) {
			host.draw_text(&format!("GPU={:.2}ms", gpu.total), WHITE);
			host.draw_text(
				&format!(
					"Sh {:.2} Pre {:.2} Cull {:.2} SSAO {:.2} Fwd {:.2} Comp {:.2}",
					gpu.shadows,
					gpu.prepass,
					gpu.light_culling,
					gpu.ssao,
					gpu.forward,
					gpu.composition
				),
				WHITE,
			);

			if gpu.probe_capture > 0.005 {
				host.draw_text(&format!("Bake {:.2}", gpu.probe_capture), WHITE);
			}
		}

		let stats = host.scene_stats();

		host.draw_text(
			&format!(
				"Vis={} Culled={}/{} Lights={}/{}",
				stats.visible, stats.culled, stats.tested, stats.lights, stats.lights_cached
			),
			WHITE,
		);

		for line in host.editor_status_lines() {
			host.draw_text(&line, GREEN);
		}
	}

	fn render_crosshair<H: Host>(host: &mut H) {
		let size = cvar::int("i_crosshair_size", DEFAULT_CROSSHAIR_SIZE);

		if size <= 0 {
			return;
		}

		let [width, height] = host.window_size();

		let position = [
			((i64::from(width) - size) / 2) as f32,
			((i64::from(height) - size) / 2) as f32,
		];

		host.draw_crosshair(position, size as f32, WHITE);
	}

	pub fn tick<H: Host>(&mut self, host: &mut H, last_tick: &mut Instant) {
		let now = Instant::now();

		self.delta_time = now.duration_since(*last_tick).as_secs_f64();

		self.fps.tick(self.delta_time, host.elapsed_frames());

		host.poll();

		if host.quit_requested() {
			self.running = false;
		}

		if self.in_command_mode {
			if host.pressed(Key::KeyEscape) {
				self.in_command_mode = false;
			}

			self.handle_console_keyboard(host);
		} else {
			self.process_controls(host);
		}

		if !self.in_command_mode {
			host.player_move(self.delta_time, Self::movement_vector(host));

			if host.editor_active() {
				host.editor_update(self.delta_time as f32);
			}
		}

		if host.editor_active() {
			host.editor_refresh_panels();
		}

		host.player_update(self.delta_time);

		host.update_sun();

		if !host.begin_frame(self.delta_time as f32) {
			*last_tick = now;
			return;
		}

		host.physics_update();

		host.gpu_read_results(self.delta_time);

		host.set_render_flags(&RenderFlags {
			disable_probes: cvar::int("r_probes", 1) == 0,
			disable_reflection_probes: cvar::int("r_reflection_probes", 1) == 0,
			reflection_debug_view: cvar::int("r_reflection_debug", 0).clamp(0, 2) as u32,
			tonemapper: cvar::int("r_tonemapper", 1).clamp(0, 1) as u32,
			disable_decals: cvar::int("r_decals", 1) == 0,
			pre_exposure: Self::exposure(),
		});

		if let Some(title) = self
			.title
			.update(&self.config.window_title, &host.blockout_path())
		{
			host.set_window_title(&title);
		}

		host.set_debug_bounds_mask(cvar::int("r_debug_bounds", 0) as u32);

		self.update_ragdoll_dummies(host);

		host.begin_commands();

		host.render_world();

		if host.did_resize() {
			log_info!("Setting aspect ratio");
			host.update_camera_aspect();
		}

		self.render_text(host);
		Self::render_crosshair(host);
		host.weapons_render_hud();

		host.compose();

		host.probes_service_bake();

		*last_tick = now;
	}

	pub fn destroy<H: Host>(&mut self, host: &mut H) {
		for dummy in self.dummies.drain() {
			Self::destroy_dummy(host, dummy.handle);
		}

		host.shutdown();
	}
}
