use raptor_core::Key;

use crate::config::HeadBob;
use crate::ragdoll::{DummyHandle, Impact};

pub mod weapon_input {
	pub const FIRE_PRESSED: u32 = 1 << 0;
	pub const FIRE_HELD: u32 = 1 << 1;
	pub const RELOAD_PRESSED: u32 = 1 << 2;
	pub const SWITCH_MODE: u32 = 1 << 3;
	pub const SWITCH_WEAPON: u32 = 1 << 4;
}

pub mod debug_bounds {
	pub const OBJECTS: u32 = 1 << 0;
	pub const LIGHTS: u32 = 1 << 1;
	pub const PHYSICS: u32 = 1 << 2;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EditorTool {
	None,
	Translate,
}

#[derive(Clone, Debug)]
pub struct Startup {
	pub window_title: String,
	pub window_width: u32,
	pub window_height: u32,
	pub head_bob: Option<HeadBob>,
	pub blockout: Option<String>,
	pub scene: Option<String>,
	pub args: Vec<String>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct RenderFlags {
	pub disable_probes: bool,
	pub disable_reflection_probes: bool,
	pub reflection_debug_view: u32,
	pub tonemapper: u32,
	pub disable_decals: bool,
	pub pre_exposure: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct GpuTimes {
	pub total: f64,
	pub shadows: f64,
	pub prepass: f64,
	pub light_culling: f64,
	pub ssao: f64,
	pub forward: f64,
	pub composition: f64,
	pub probe_capture: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SceneStats {
	pub visible: u32,
	pub culled: u32,
	pub tested: u32,
	pub lights: u32,
	pub lights_cached: u32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RagdollSpawn {
	pub handle: DummyHandle,
	pub serial: u64,
}

pub trait Input {
	fn poll(&mut self);
	fn down(&self, key: Key) -> bool;
	fn pressed(&self, key: Key) -> bool;
	fn mouse_locked(&self) -> bool;
	fn capture_mouse(&mut self);
	fn release_mouse(&mut self);
	fn mouse_delta(&self) -> [f32; 2];
	fn typed_char(&mut self) -> Option<char>;
	fn quit_requested(&self) -> bool;

	fn combo_pressed(&self, a: Key, b: Key) -> bool {
		(self.pressed(a) && self.down(b)) || (self.down(a) && self.pressed(b))
	}
}

pub trait Host: Input {
	fn startup(&mut self, setup: &Startup) -> Result<(), String>;
	fn begin_game(&mut self, scene: Option<&str>) -> Result<(), String>;
	fn shutdown(&mut self);

	fn window_focused(&self) -> bool;
	fn window_size(&self) -> [u32; 2];
	fn set_window_title(&mut self, title: &str);
	fn did_resize(&self) -> bool;
	fn update_camera_aspect(&mut self);

	fn blockout_path(&self) -> String;
	fn reload_blockout(&mut self);
	fn save_blockout(&mut self, path: &str);
	fn reload_scripts(&mut self);
	fn random_unit(&mut self) -> f32;

	fn editor_active(&self) -> bool;
	fn editor_simulation_mode(&self) -> bool;
	fn editor_set_tool(&mut self, tool: EditorTool);
	fn editor_update(&mut self, delta_time: f32);
	fn editor_refresh_panels(&mut self);
	fn editor_status_lines(&self) -> Vec<String>;
	fn editor_run_command(&mut self, name: &str) -> bool;

	fn player_position(&self) -> [f32; 3];
	fn camera_forward(&self) -> [f32; 3];
	fn camera_right(&self) -> [f32; 3];
	fn player_move(&mut self, delta_time: f64, movement: [f32; 3]);
	fn player_update(&mut self, delta_time: f64);
	fn player_rotate_head(&mut self, delta: [f32; 2]);
	fn player_jump(&mut self);
	fn player_fly_mode(&self) -> bool;
	fn player_set_fly_mode(&mut self, fly: bool);
	fn player_set_sprinting(&mut self, sprinting: bool);

	fn weapons_set_enabled(&mut self, enabled: bool);
	fn weapons_set_input(&mut self, flags: u32);
	fn weapons_render_hud(&mut self);

	fn toggle_render_probes(&mut self) -> bool;
	fn toggle_only_render_probes(&mut self) -> bool;
	fn toggle_probe_visibility(&mut self) -> bool;
	fn set_debug_bounds_mask(&mut self, mask: u32);
	fn log_nearby_objects(&self);
	fn log_player_tile(&self);

	fn probes_rebuild_and_bake(&mut self);
	fn probes_save(&mut self);
	fn probes_service_bake(&mut self);

	fn update_sun(&mut self);
	fn begin_frame(&mut self, delta_time: f32) -> bool;
	fn elapsed_frames(&self) -> u64;
	fn physics_update(&mut self);
	fn gpu_read_results(&mut self, delta_time: f64);
	fn gpu_times(&self) -> Option<GpuTimes>;
	fn set_render_flags(&mut self, flags: &RenderFlags);
	fn begin_commands(&mut self);
	fn render_world(&mut self);
	fn compose(&mut self);
	fn scene_stats(&self) -> SceneStats;

	fn draw_text(&mut self, text: &str, color: u32);
	fn draw_crosshair(&mut self, position: [f32; 2], size: f32, color: u32);

	fn ragdoll_request_template(&mut self);
	fn ragdoll_template_ready(&self) -> bool;
	fn ragdoll_spawn(
		&mut self,
		position: [f32; 3],
		forward: [f32; 3],
		spawn_index: u32,
	) -> Option<RagdollSpawn>;
	fn ragdoll_destroy(&mut self, handle: DummyHandle);
	fn ragdoll_sync(&mut self, handle: DummyHandle, debug_draw: bool);
	fn ragdoll_drain_impacts(&mut self, min_speed: f32) -> Vec<Impact>;
	fn add_blood_splat(&mut self, point: [f32; 3], normal: [f32; 3], size: f32);
}
