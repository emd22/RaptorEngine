use std::collections::HashMap;
use std::ffi::{CString, c_char, c_int};

use raptor_core::cvar::{self, CVarValue};
use raptor_core::Key;
use raptor_game::host::{
	EditorTool, GpuTimes, Host, Input, RagdollSpawn, RenderFlags, SceneStats, Startup,
};
use raptor_game::ragdoll::{DummyHandle, Impact};

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RhImpact
{
	serial: u64,
	point: [f32; 3],
	normal: [f32; 3],
	speed: f32,
}

unsafe extern "C" {
	fn rh_startup(
		argc: c_int,
		argv: *mut *mut c_char,
		title: *const c_char,
		width: u32,
		height: u32,
		has_bob: c_int,
		bob_enabled: c_int,
		bob_x: f32,
		bob_y: f32,
		blockout: *const c_char,
	) -> c_int;
	fn rh_begin_game(scene: *const c_char) -> c_int;
	fn rh_shutdown();
	fn rh_poll();
	fn rh_key_down(key: u16) -> bool;
	fn rh_key_pressed(key: u16) -> bool;
	fn rh_mouse_locked() -> bool;
	fn rh_capture_mouse();
	fn rh_release_mouse();
	fn rh_mouse_delta(out: *mut f32);
	fn rh_typed_char() -> u32;
	fn rh_quit_requested() -> bool;
	fn rh_window_focused() -> bool;
	fn rh_window_size(out: *mut u32);
	fn rh_set_window_title(title: *const c_char);
	fn rh_did_resize() -> bool;
	fn rh_update_camera_aspect();
	fn rh_blockout_path(buffer: *mut c_char, capacity: usize) -> usize;
	fn rh_reload_blockout();
	fn rh_save_blockout(path: *const c_char);
	fn rh_reload_scripts();
	fn rh_random_unit() -> f32;
	fn rh_editor_active() -> bool;
	fn rh_editor_simulation_mode() -> bool;
	fn rh_editor_set_tool(tool: c_int);
	fn rh_editor_update(delta_time: f32);
	fn rh_editor_refresh_panels();
	fn rh_editor_status_lines(buffer: *mut c_char, capacity: usize) -> usize;
	fn rh_editor_run_command(name: *const c_char) -> bool;
	fn rh_player_position(out: *mut f32);
	fn rh_camera_forward(out: *mut f32);
	fn rh_camera_right(out: *mut f32);
	fn rh_player_move(delta_time: f64, movement: *const f32);
	fn rh_player_update(delta_time: f64);
	fn rh_player_rotate_head(x: f32, y: f32);
	fn rh_player_jump();
	fn rh_player_fly_mode() -> bool;
	fn rh_player_set_fly_mode(fly: bool);
	fn rh_player_set_sprinting(sprinting: bool);
	fn rh_weapons_set_enabled(enabled: bool);
	fn rh_weapons_set_input(flags: u32);
	fn rh_weapons_render_hud();
	fn rh_toggle_render_probes() -> bool;
	fn rh_toggle_only_render_probes() -> bool;
	fn rh_toggle_probe_visibility() -> bool;
	fn rh_set_debug_bounds_mask(mask: u32);
	fn rh_log_nearby_objects();
	fn rh_log_player_tile();
	fn rh_probes_rebuild_and_bake();
	fn rh_probes_save();
	fn rh_probes_service_bake();
	fn rh_update_sun();
	fn rh_begin_frame(delta_time: f32) -> bool;
	fn rh_elapsed_frames() -> u64;
	fn rh_physics_update();
	fn rh_gpu_read_results(delta_time: f64);
	fn rh_gpu_times(out: *mut f64) -> bool;
	fn rh_set_render_flags(
		disable_probes: bool,
		disable_reflection_probes: bool,
		reflection_debug_view: u32,
		tonemapper: u32,
		disable_decals: bool,
		pre_exposure: f32,
	);
	fn rh_begin_commands();
	fn rh_render_world();
	fn rh_compose();
	fn rh_scene_stats(out: *mut u32);
	fn rh_draw_text(text: *const c_char, color: u32);
	fn rh_draw_crosshair(x: f32, y: f32, size: f32, color: u32);
	fn rh_cvar_mirror_int(name: *const c_char, value: i64);
	fn rh_cvar_mirror_float(name: *const c_char, value: f32);
	fn rh_cvar_mirror_bool(name: *const c_char, value: bool);
	fn rh_cvar_mirror_string(name: *const c_char, value: *const c_char);
	fn rh_ragdoll_request_template();
	fn rh_ragdoll_template_ready() -> bool;
	fn rh_ragdoll_spawn(
		position: *const f32,
		forward: *const f32,
		index: u32,
		out_handle: *mut u32,
		out_serial: *mut u64,
	) -> bool;
	fn rh_ragdoll_destroy(handle: u32);
	fn rh_ragdoll_sync(handle: u32, debug_draw: bool);
	fn rh_ragdoll_drain_impacts(min_speed: f32, out: *mut RhImpact, capacity: usize) -> usize;
	fn rh_add_blood_splat(point: *const f32, normal: *const f32, size: f32);
}

fn cstring(text: &str) -> CString
{
	CString::new(text.replace('\0', "")).unwrap_or_default()
}

pub struct CppHost
{
	mirrored: HashMap<String, CVarValue>,
}

impl CppHost
{
	pub fn new() -> Self
	{
		Self {
			mirrored: HashMap::new(),
		}
	}

	fn mirror_cvars(&mut self)
	{
		for (name, value) in cvar::snapshot() {
			if self.mirrored.get(&name) == Some(&value) {
				continue;
			}

			let c_name = cstring(&name);

			// SAFETY: `c_name` and any string value are NUL terminated and outlive the call.
			unsafe {
				match &value {
					CVarValue::Int(value) => rh_cvar_mirror_int(c_name.as_ptr(), *value),
					CVarValue::Float(value) => rh_cvar_mirror_float(c_name.as_ptr(), *value),
					CVarValue::Bool(value) => rh_cvar_mirror_bool(c_name.as_ptr(), *value),
					CVarValue::Str(value) => {
						rh_cvar_mirror_string(c_name.as_ptr(), cstring(value).as_ptr());
					}
				}
			}

			self.mirrored.insert(name, value);
		}
	}

	fn read_string(read: impl Fn(*mut c_char, usize) -> usize) -> String
	{
		let length = read(std::ptr::null_mut(), 0);

		if length == 0 {
			return String::new();
		}

		let mut buffer = vec![0u8; length];

		read(buffer.as_mut_ptr().cast(), length);

		String::from_utf8_lossy(&buffer).into_owned()
	}

	fn vec3(read: unsafe extern "C" fn(*mut f32)) -> [f32; 3]
	{
		let mut out = [0.0f32; 3];

		// SAFETY: `out` has room for the three floats the callee writes.
		unsafe { read(out.as_mut_ptr()) };

		out
	}
}

impl Input for CppHost
{
	fn poll(&mut self)
	{
		// SAFETY: plain engine call with no arguments.
		unsafe { rh_poll() };
	}

	fn down(&self, key: Key) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_key_down(key.code()) }
	}

	fn pressed(&self, key: Key) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_key_pressed(key.code()) }
	}

	fn mouse_locked(&self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_mouse_locked() }
	}

	fn capture_mouse(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_capture_mouse() };
	}

	fn release_mouse(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_release_mouse() };
	}

	fn mouse_delta(&self) -> [f32; 2]
	{
		let mut out = [0.0f32; 2];

		// SAFETY: `out` has room for the two floats the callee writes.
		unsafe { rh_mouse_delta(out.as_mut_ptr()) };

		out
	}

	fn typed_char(&mut self) -> Option<char>
	{
		// SAFETY: plain engine call.
		let code = unsafe { rh_typed_char() };

		char::from_u32(code).filter(|ch| *ch != '\0')
	}

	fn quit_requested(&self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_quit_requested() }
	}
}

impl Host for CppHost
{
	fn startup(&mut self, setup: &Startup) -> Result<(), String>
	{
		let args: Vec<CString> = setup.args.iter().map(|arg| cstring(arg)).collect();
		let mut argv: Vec<*mut c_char> = args.iter().map(|arg| arg.as_ptr().cast_mut()).collect();

		argv.push(std::ptr::null_mut());

		let title = cstring(&setup.window_title);
		let blockout = setup.blockout.as_deref().map(cstring);
		let bob = setup.head_bob.as_ref();

		// SAFETY: `argv` is a null terminated array of `args.len()` NUL terminated strings and the other pointers are
		// NUL terminated or null; all outlive the call.
		let started = unsafe {
			rh_startup(
				args.len() as c_int,
				argv.as_mut_ptr(),
				title.as_ptr(),
				setup.window_width,
				setup.window_height,
				c_int::from(bob.is_some()),
				c_int::from(bob.is_some_and(|bob| bob.enabled)),
				bob.map_or(0.0, |bob| bob.scale_x),
				bob.map_or(0.0, |bob| bob.scale_y),
				blockout.as_ref().map_or(std::ptr::null(), |text| text.as_ptr()),
			)
		};

		if started == 0 {
			return Err("the engine could not start".to_owned());
		}

		Ok(())
	}

	fn begin_game(&mut self, scene: Option<&str>) -> Result<(), String>
	{
		let scene = scene.map(cstring);

		self.mirror_cvars();

		// SAFETY: `scene` is NUL terminated or null and outlives the call.
		let created = unsafe { rh_begin_game(scene.as_ref().map_or(std::ptr::null(), |text| text.as_ptr())) };

		if created == 0 {
			return Err("the game could not be created".to_owned());
		}

		Ok(())
	}

	fn shutdown(&mut self)
	{
		// SAFETY: called once after the main loop ends.
		unsafe { rh_shutdown() };
	}

	fn window_focused(&self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_window_focused() }
	}

	fn window_size(&self) -> [u32; 2]
	{
		let mut out = [0u32; 2];

		// SAFETY: `out` has room for the two integers the callee writes.
		unsafe { rh_window_size(out.as_mut_ptr()) };

		out
	}

	fn set_window_title(&mut self, title: &str)
	{
		let title = cstring(title);

		// SAFETY: `title` is NUL terminated and outlives the call.
		unsafe { rh_set_window_title(title.as_ptr()) };
	}

	fn did_resize(&self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_did_resize() }
	}

	fn update_camera_aspect(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_update_camera_aspect() };
	}

	fn blockout_path(&self) -> String
	{
		// SAFETY: the callee writes at most `capacity` bytes into the buffer it is given.
		Self::read_string(|buffer, capacity| unsafe { rh_blockout_path(buffer, capacity) })
	}

	fn reload_blockout(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_reload_blockout() };
	}

	fn save_blockout(&mut self, path: &str)
	{
		let path = cstring(path);

		// SAFETY: `path` is NUL terminated and outlives the call.
		unsafe { rh_save_blockout(path.as_ptr()) };
	}

	fn reload_scripts(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_reload_scripts() };
	}

	fn random_unit(&mut self) -> f32
	{
		// SAFETY: plain engine call.
		unsafe { rh_random_unit() }
	}

	fn editor_active(&self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_editor_active() }
	}

	fn editor_simulation_mode(&self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_editor_simulation_mode() }
	}

	fn editor_set_tool(&mut self, tool: EditorTool)
	{
		// SAFETY: plain engine call.
		unsafe { rh_editor_set_tool(c_int::from(tool == EditorTool::Translate)) };
	}

	fn editor_update(&mut self, delta_time: f32)
	{
		// SAFETY: plain engine call.
		unsafe { rh_editor_update(delta_time) };
	}

	fn editor_refresh_panels(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_editor_refresh_panels() };
	}

	fn editor_status_lines(&self) -> Vec<String>
	{
		// SAFETY: the callee writes at most `capacity` bytes into the buffer it is given.
		let text = Self::read_string(|buffer, capacity| unsafe { rh_editor_status_lines(buffer, capacity) });

		text.lines().map(str::to_owned).collect()
	}

	fn editor_run_command(&mut self, name: &str) -> bool
	{
		let name = cstring(name);

		// SAFETY: `name` is NUL terminated and outlives the call.
		unsafe { rh_editor_run_command(name.as_ptr()) }
	}

	fn player_position(&self) -> [f32; 3]
	{
		Self::vec3(rh_player_position)
	}

	fn camera_forward(&self) -> [f32; 3]
	{
		Self::vec3(rh_camera_forward)
	}

	fn camera_right(&self) -> [f32; 3]
	{
		Self::vec3(rh_camera_right)
	}

	fn player_move(&mut self, delta_time: f64, movement: [f32; 3])
	{
		// SAFETY: `movement` is three floats.
		unsafe { rh_player_move(delta_time, movement.as_ptr()) };
	}

	fn player_update(&mut self, delta_time: f64)
	{
		// SAFETY: plain engine call.
		unsafe { rh_player_update(delta_time) };
	}

	fn player_rotate_head(&mut self, delta: [f32; 2])
	{
		// SAFETY: plain engine call.
		unsafe { rh_player_rotate_head(delta[0], delta[1]) };
	}

	fn player_jump(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_player_jump() };
	}

	fn player_fly_mode(&self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_player_fly_mode() }
	}

	fn player_set_fly_mode(&mut self, fly: bool)
	{
		// SAFETY: plain engine call.
		unsafe { rh_player_set_fly_mode(fly) };
	}

	fn player_set_sprinting(&mut self, sprinting: bool)
	{
		// SAFETY: plain engine call.
		unsafe { rh_player_set_sprinting(sprinting) };
	}

	fn weapons_set_enabled(&mut self, enabled: bool)
	{
		// SAFETY: plain engine call.
		unsafe { rh_weapons_set_enabled(enabled) };
	}

	fn weapons_set_input(&mut self, flags: u32)
	{
		// SAFETY: plain engine call.
		unsafe { rh_weapons_set_input(flags) };
	}

	fn weapons_render_hud(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_weapons_render_hud() };
	}

	fn toggle_render_probes(&mut self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_toggle_render_probes() }
	}

	fn toggle_only_render_probes(&mut self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_toggle_only_render_probes() }
	}

	fn toggle_probe_visibility(&mut self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_toggle_probe_visibility() }
	}

	fn set_debug_bounds_mask(&mut self, mask: u32)
	{
		// SAFETY: plain engine call.
		unsafe { rh_set_debug_bounds_mask(mask) };
	}

	fn log_nearby_objects(&self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_log_nearby_objects() };
	}

	fn log_player_tile(&self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_log_player_tile() };
	}

	fn probes_rebuild_and_bake(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_probes_rebuild_and_bake() };
	}

	fn probes_save(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_probes_save() };
	}

	fn probes_service_bake(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_probes_service_bake() };
	}

	fn update_sun(&mut self)
	{
		self.mirror_cvars();

		// SAFETY: plain engine call.
		unsafe { rh_update_sun() };
	}

	fn begin_frame(&mut self, delta_time: f32) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_begin_frame(delta_time) }
	}

	fn elapsed_frames(&self) -> u64
	{
		// SAFETY: plain engine call.
		unsafe { rh_elapsed_frames() }
	}

	fn physics_update(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_physics_update() };
	}

	fn gpu_read_results(&mut self, delta_time: f64)
	{
		// SAFETY: plain engine call.
		unsafe { rh_gpu_read_results(delta_time) };
	}

	fn gpu_times(&self) -> Option<GpuTimes>
	{
		let mut out = [0.0f64; 8];

		// SAFETY: `out` has room for the eight doubles the callee writes.
		let enabled = unsafe { rh_gpu_times(out.as_mut_ptr()) };

		enabled.then_some(GpuTimes {
			total: out[0],
			shadows: out[1],
			prepass: out[2],
			light_culling: out[3],
			ssao: out[4],
			forward: out[5],
			composition: out[6],
			probe_capture: out[7],
		})
	}

	fn set_render_flags(&mut self, flags: &RenderFlags)
	{
		// SAFETY: plain engine call.
		unsafe {
			rh_set_render_flags(
				flags.disable_probes,
				flags.disable_reflection_probes,
				flags.reflection_debug_view,
				flags.tonemapper,
				flags.disable_decals,
				flags.pre_exposure,
			);
		}
	}

	fn begin_commands(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_begin_commands() };
	}

	fn render_world(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_render_world() };
	}

	fn compose(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_compose() };
	}

	fn scene_stats(&self) -> SceneStats
	{
		let mut out = [0u32; 5];

		// SAFETY: `out` has room for the five integers the callee writes.
		unsafe { rh_scene_stats(out.as_mut_ptr()) };

		SceneStats {
			visible: out[0],
			culled: out[1],
			tested: out[2],
			lights: out[3],
			lights_cached: out[4],
		}
	}

	fn draw_text(&mut self, text: &str, color: u32)
	{
		let text = cstring(text);

		// SAFETY: `text` is NUL terminated and outlives the call.
		unsafe { rh_draw_text(text.as_ptr(), color) };
	}

	fn draw_crosshair(&mut self, position: [f32; 2], size: f32, color: u32)
	{
		// SAFETY: plain engine call.
		unsafe { rh_draw_crosshair(position[0], position[1], size, color) };
	}

	fn ragdoll_request_template(&mut self)
	{
		// SAFETY: plain engine call.
		unsafe { rh_ragdoll_request_template() };
	}

	fn ragdoll_template_ready(&self) -> bool
	{
		// SAFETY: plain engine call.
		unsafe { rh_ragdoll_template_ready() }
	}

	fn ragdoll_spawn(&mut self, position: [f32; 3], forward: [f32; 3], spawn_index: u32) -> Option<RagdollSpawn>
	{
		let mut handle = 0u32;
		let mut serial = 0u64;

		// SAFETY: the vectors are three floats and the outputs are valid for writes.
		let spawned = unsafe {
			rh_ragdoll_spawn(position.as_ptr(), forward.as_ptr(), spawn_index, &mut handle, &mut serial)
		};

		spawned.then_some(RagdollSpawn {
			handle: DummyHandle(handle),
			serial,
		})
	}

	fn ragdoll_destroy(&mut self, handle: DummyHandle)
	{
		// SAFETY: plain engine call.
		unsafe { rh_ragdoll_destroy(handle.0) };
	}

	fn ragdoll_sync(&mut self, handle: DummyHandle, debug_draw: bool)
	{
		// SAFETY: plain engine call.
		unsafe { rh_ragdoll_sync(handle.0, debug_draw) };
	}

	fn ragdoll_drain_impacts(&mut self, min_speed: f32) -> Vec<Impact>
	{
		let mut impacts = [RhImpact::default(); 64];

		// SAFETY: `impacts` is valid for `impacts.len()` writes.
		let count = unsafe { rh_ragdoll_drain_impacts(min_speed, impacts.as_mut_ptr(), impacts.len()) };

		impacts[..count.min(impacts.len())]
			.iter()
			.map(|impact| Impact {
				serial: impact.serial,
				point: impact.point,
				normal: impact.normal,
				speed: impact.speed,
			})
			.collect()
	}

	fn add_blood_splat(&mut self, point: [f32; 3], normal: [f32; 3], size: f32)
	{
		// SAFETY: both vectors are three floats.
		unsafe { rh_add_blood_splat(point.as_ptr(), normal.as_ptr(), size) };
	}
}
