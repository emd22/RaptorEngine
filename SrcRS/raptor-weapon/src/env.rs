#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CameraBasis {
	pub position: [f32; 3],
	pub forward: [f32; 3],
	pub right: [f32; 3],
	pub up: [f32; 3],
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SurfaceHit {
	pub body: u32,
	pub point: [f32; 3],
	pub normal: [f32; 3],
}

/// What the weapons need of the rest of the game.
pub trait WeaponEnv {
	fn camera(&self) -> CameraBasis;
	fn player_velocity(&self) -> [f32; 3];
	fn player_grounded(&self) -> bool;
	fn player_fly_mode(&self) -> bool;

	fn set_view_model_animations(&mut self, idle: &str, fire: &str, reload: &str);
	fn set_recoil_recovery(&mut self, rate_per_second: f32);
	fn set_view_model_holster(&mut self, amount: f32);
	fn do_fire_animation(&mut self, kick_degrees: f32);
	fn do_reload_animation(&mut self);
	fn add_recoil(&mut self, pitch: f32, yaw: f32);

	fn raycast(
		&self,
		origin: [f32; 3],
		direction: [f32; 3],
		ignore: Option<u32>,
	) -> Option<SurfaceHit>;
	fn body_is_dynamic(&self, body: u32) -> bool;
	fn body_bleeds(&self, body: u32) -> bool;
	fn push_body(&mut self, body: u32, impulse: [f32; 3]);

	fn add_bullet_hole(&mut self, point: [f32; 3], normal: [f32; 3]);
	fn add_blood_splat(&mut self, point: [f32; 3], normal: [f32; 3], size: f32);

	fn random_unit(&mut self) -> f32;

	fn window_size(&self) -> [u32; 2];
	fn glyph_size(&self) -> [f32; 2];
	fn draw_text(&mut self, text: &str, position: [f32; 2], scale: f32, color: u32);

	fn debug_enabled(&self) -> bool;
	fn log(&mut self, message: &str);
}
