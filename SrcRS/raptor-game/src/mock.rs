use std::collections::HashSet;

use raptor_core::Key;

use crate::host::*;
use crate::ragdoll::{DummyHandle, Impact};

#[derive(Default)]
pub struct MockHost {
	pub down: HashSet<Key>,
	pub pressed: HashSet<Key>,
	pub locked: bool,
	pub text: Vec<(String, u32)>,
	pub frames: u64,
	pub begin_frame_ok: bool,
	pub spawned: Vec<(DummyHandle, [f32; 3])>,
	pub destroyed: Vec<DummyHandle>,
	pub blood: Vec<([f32; 3], f32)>,
	pub impacts: Vec<Impact>,
	pub template_requests: u32,
	pub template_ready: bool,
	pub titles: Vec<String>,
	pub rendered: u32,
	pub composed: u32,
	pub sprinting: bool,
	pub fly: bool,
	pub typed: Vec<char>,
	pub quit: bool,
	pub saved_blockouts: Vec<String>,
	pub weapon_flags: Vec<u32>,
	pub commands: Vec<String>,
	pub crosshair: Vec<([f32; 2], f32)>,
}

impl MockHost {
	pub fn new() -> Self {
		Self {
			begin_frame_ok: true,
			..Self::default()
		}
	}
}

impl Input for MockHost {
	fn poll(&mut self) {}
	fn down(&self, key: Key) -> bool {
		self.down.contains(&key) || self.pressed.contains(&key)
	}
	fn pressed(&self, key: Key) -> bool {
		self.pressed.contains(&key)
	}
	fn mouse_locked(&self) -> bool {
		self.locked
	}
	fn capture_mouse(&mut self) {
		self.locked = true;
	}
	fn release_mouse(&mut self) {
		self.locked = false;
	}
	fn mouse_delta(&self) -> [f32; 2] {
		[0.0; 2]
	}
	fn typed_char(&mut self) -> Option<char> {
		if self.typed.is_empty() {
			None
		} else {
			Some(self.typed.remove(0))
		}
	}
	fn quit_requested(&self) -> bool {
		self.quit
	}
}

impl Host for MockHost {
	fn startup(&mut self, _setup: &Startup) -> Result<(), String> {
		Ok(())
	}
	fn begin_game(&mut self, _scene: Option<&str>) -> Result<(), String> {
		Ok(())
	}
	fn shutdown(&mut self) {}
	fn window_focused(&self) -> bool {
		true
	}
	fn window_size(&self) -> [u32; 2] {
		[900, 640]
	}
	fn set_window_title(&mut self, title: &str) {
		self.titles.push(title.to_owned());
	}
	fn did_resize(&self) -> bool {
		false
	}
	fn update_camera_aspect(&mut self) {}
	fn blockout_path(&self) -> String {
		"RaptorData/Data/blockouts/map_test.prx".to_owned()
	}
	fn reload_blockout(&mut self) {}
	fn save_blockout(&mut self, path: &str) {
		self.saved_blockouts.push(path.to_owned());
	}
	fn reload_scripts(&mut self) {}
	fn random_unit(&mut self) -> f32 {
		0.5
	}
	fn editor_active(&self) -> bool {
		false
	}
	fn editor_simulation_mode(&self) -> bool {
		true
	}
	fn editor_set_tool(&mut self, _tool: EditorTool) {}
	fn editor_update(&mut self, _delta_time: f32) {}
	fn editor_refresh_panels(&mut self) {}
	fn editor_status_lines(&self) -> Vec<String> {
		Vec::new()
	}
	fn editor_run_command(&mut self, name: &str) -> bool {
		self.commands.push(name.to_owned());
		true
	}
	fn player_position(&self) -> [f32; 3] {
		[0.0; 3]
	}
	fn camera_forward(&self) -> [f32; 3] {
		[0.0, 0.0, 1.0]
	}
	fn camera_right(&self) -> [f32; 3] {
		[1.0, 0.0, 0.0]
	}
	fn player_move(&mut self, _delta_time: f64, _movement: [f32; 3]) {}
	fn player_update(&mut self, _delta_time: f64) {}
	fn player_rotate_head(&mut self, _delta: [f32; 2]) {}
	fn player_jump(&mut self) {}
	fn player_fly_mode(&self) -> bool {
		self.fly
	}
	fn player_set_fly_mode(&mut self, fly: bool) {
		self.fly = fly;
	}
	fn player_set_sprinting(&mut self, sprinting: bool) {
		self.sprinting = sprinting;
	}
	fn weapons_set_enabled(&mut self, _enabled: bool) {}
	fn weapons_set_input(&mut self, flags: u32) {
		self.weapon_flags.push(flags);
	}
	fn weapons_render_hud(&mut self) {}
	fn toggle_render_probes(&mut self) -> bool {
		true
	}
	fn toggle_only_render_probes(&mut self) -> bool {
		true
	}
	fn toggle_probe_visibility(&mut self) -> bool {
		true
	}
	fn set_debug_bounds_mask(&mut self, _mask: u32) {}
	fn log_nearby_objects(&mut self) {}
	fn log_player_tile(&self) {}
	fn probes_rebuild_and_bake(&mut self) {}
	fn probes_save(&mut self) {}
	fn probes_service_bake(&mut self) {}
	fn update_sun(&mut self) {}
	fn begin_frame(&mut self, _delta_time: f32) -> bool {
		if self.begin_frame_ok {
			self.frames += 1;
		}
		self.begin_frame_ok
	}
	fn elapsed_frames(&self) -> u64 {
		self.frames
	}
	fn physics_update(&mut self) {}
	fn gpu_read_results(&mut self, _delta_time: f64) {}
	fn gpu_times(&self) -> Option<GpuTimes> {
		None
	}
	fn set_render_flags(&mut self, _flags: &RenderFlags) {}
	fn begin_commands(&mut self) {}
	fn render_world(&mut self) {
		self.rendered += 1;
	}
	fn compose(&mut self) {
		self.composed += 1;
	}
	fn scene_stats(&self) -> SceneStats {
		SceneStats::default()
	}
	fn draw_text(&mut self, text: &str, color: u32) {
		self.text.push((text.to_owned(), color));
	}
	fn draw_crosshair(&mut self, position: [f32; 2], size: f32, _color: u32) {
		self.crosshair.push((position, size));
	}
	fn ragdoll_request_template(&mut self) {
		self.template_requests += 1;
	}
	fn ragdoll_template_ready(&self) -> bool {
		self.template_ready
	}
	fn ragdoll_spawn(
		&mut self,
		position: [f32; 3],
		_forward: [f32; 3],
		spawn_index: u32,
	) -> Option<RagdollSpawn> {
		let handle = DummyHandle(spawn_index);

		self.spawned.push((handle, position));

		Some(RagdollSpawn {
			handle,
			serial: u64::from(spawn_index) + 100,
		})
	}
	fn ragdoll_destroy(&mut self, handle: DummyHandle) {
		self.destroyed.push(handle);
	}
	fn ragdoll_sync(&mut self, _handle: DummyHandle, _debug_draw: bool) {}
	fn ragdoll_drain_impacts(&mut self, _min_speed: f32) -> Vec<Impact> {
		std::mem::take(&mut self.impacts)
	}
	fn add_blood_splat(&mut self, point: [f32; 3], _normal: [f32; 3], size: f32) {
		self.blood.push((point, size));
	}
}
