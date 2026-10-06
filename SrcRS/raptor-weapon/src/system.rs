use std::path::Path;

use raptor_core::Color;
use raptor_level::access::FsHost;
use raptor_level::{ScriptDef, WeaponDef, WeaponError};

use crate::env::{SurfaceHit, WeaponEnv};
use crate::state::{
	AIRBORNE, Event, FIRE_HELD, FIRE_PRESSED, FireMode, InputFlags, Phase, SWITCH_WEAPON,
	ScriptInput, ScriptState,
};

pub const MAX_WEAPONS: usize = 8;

pub const WEAPON_DIRECTORY: &str = "RaptorData/Data/Weapons";
pub const CONSTANTS_PATH: &str = "Config/Internal/Constants.conf";

const SPRINT_SPEED: f32 = 5.0;

pub trait ScriptHost {
	fn trace(&mut self, yaw: f32, pitch: f32, range: f32) -> f32;
	fn apply_hit(&mut self, damage: f32, force: f32, decal: bool);
	fn kick(&mut self, pitch: f32, yaw: f32);
	fn event(&mut self, event: Event);
}

/// The functions of a weapon's script. Each one reports whether the script has it.
pub trait WeaponScript {
	fn init(&mut self, def: &ScriptDef, state: &mut ScriptState, host: &mut dyn ScriptHost);

	fn equip(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		host: &mut dyn ScriptHost,
	) -> bool;

	fn holster(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		host: &mut dyn ScriptHost,
	) -> bool;

	fn update(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		input: &ScriptInput,
		delta_time: f32,
		host: &mut dyn ScriptHost,
	) -> bool;

	fn rebind(&mut self);
}

pub struct Weapon<S> {
	pub name: String,
	pub slot: i32,
	pub script_path: String,
	pub idle_anim: String,
	pub fire_anim: String,
	pub reload_anim: String,
	pub view_kick_degrees: f32,
	pub def: ScriptDef,
	pub state: ScriptState,
	pub script: S,
}

struct Core {
	active: usize,
	pending: usize,
	input: ScriptInput,
	fire_latched: bool,
	switching: bool,
	enabled: bool,
	last_hit: Option<SurfaceHit>,
	last_direction: [f32; 3],
}

pub struct WeaponSystem<S> {
	weapons: Vec<Weapon<S>>,
	core: Core,
}

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
	[a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn length(a: [f32; 3]) -> f32 {
	(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt()
}

fn normalize(a: [f32; 3]) -> [f32; 3] {
	let length = length(a);

	if length <= f32::EPSILON {
		return [0.0; 3];
	}

	[a[0] / length, a[1] / length, a[2] / length]
}

fn scaled(a: [f32; 3], factor: f32) -> [f32; 3] {
	[a[0] * factor, a[1] * factor, a[2] * factor]
}

fn add(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
	[a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn pack(r: u8, g: u8, b: u8) -> u32 {
	Color::from_rgba(r, g, b, 255).0
}

struct Ctx<'a> {
	env: &'a mut dyn WeaponEnv,
	core: &'a mut Core,
	name: &'a str,
	view_kick: f32,
}

impl Ctx<'_> {
	fn spawn_blood_splat(&mut self, hit: SurfaceHit, is_static: bool) {
		const EXIT_RANGE: f32 = 3.0;
		const FLOOR_RANGE: f32 = 2.0;

		if is_static {
			self.env.add_blood_splat(hit.point, hit.normal, 0.5);
		}

		let scatter = [
			self.env.random_unit() - 0.5,
			self.env.random_unit() - 0.5,
			self.env.random_unit() - 0.5,
		];

		let exit_direction = normalize(add(self.core.last_direction, scaled(scatter, 0.2)));

		if let Some(exit) = self.env.raycast(
			hit.point,
			scaled(exit_direction, EXIT_RANGE),
			Some(hit.body),
		) {
			let fade = 1.0 - 0.6 * (length(sub(exit.point, hit.point)) / EXIT_RANGE);

			self.env
				.add_blood_splat(exit.point, exit.normal, 0.9 * fade);
		}

		let floor_origin = add(hit.point, scaled(hit.normal, 0.05));

		if let Some(floor) =
			self.env
				.raycast(floor_origin, [0.0, -FLOOR_RANGE, 0.0], Some(hit.body))
		{
			let fade = 1.0 - 0.5 * (length(sub(floor.point, floor_origin)) / FLOOR_RANGE);

			self.env
				.add_blood_splat(floor.point, floor.normal, 0.6 * fade);
		}
	}
}

impl ScriptHost for Ctx<'_> {
	fn trace(&mut self, yaw: f32, pitch: f32, range: f32) -> f32 {
		self.core.last_hit = None;

		let camera = self.env.camera();

		let direction = normalize(add(
			add(camera.forward, scaled(camera.right, yaw.tan())),
			scaled(camera.up, pitch.tan()),
		));

		self.core.last_direction = direction;

		let Some(hit) = self
			.env
			.raycast(camera.position, scaled(direction, range), None)
		else {
			return -1.0;
		};

		self.core.last_hit = Some(hit);

		length(sub(hit.point, camera.position))
	}

	fn apply_hit(&mut self, damage: f32, force: f32, decal: bool) {
		let Some(hit) = self.core.last_hit else {
			return;
		};

		let is_static = !self.env.body_is_dynamic(hit.body);
		let bleeds = self.env.body_bleeds(hit.body);

		if bleeds {
			if decal {
				self.spawn_blood_splat(hit, is_static);
			}
		} else if is_static && decal {
			self.env.add_bullet_hole(hit.point, hit.normal);
		}

		if !is_static && force > 0.0 {
			self.env
				.push_body(hit.body, scaled(self.core.last_direction, force));
		}

		if self.env.debug_enabled() {
			self.env
				.log(&format!("hit for {damage:.1} damage at {:?}", hit.point));
		}
	}

	fn kick(&mut self, pitch: f32, yaw: f32) {
		self.env.add_recoil(pitch, yaw);
	}

	fn event(&mut self, event: Event) {
		match event {
			Event::Fire => self.env.do_fire_animation(self.view_kick),
			Event::ReloadBegin => self.env.do_reload_animation(),
			_ => {}
		}

		if self.env.debug_enabled() {
			let message = format!("[{}] {}", self.name, event.name());

			self.env.log(&message);
		}
	}
}

impl<S: WeaponScript> WeaponSystem<S> {
	pub fn new() -> Self {
		Self {
			weapons: Vec::new(),
			core: Core {
				active: 0,
				pending: 0,
				input: ScriptInput::default(),
				fire_latched: false,
				switching: false,
				enabled: true,
				last_hit: None,
				last_direction: [0.0; 3],
			},
		}
	}

	pub fn weapon_count(&self) -> usize {
		self.weapons.len()
	}

	pub fn active_weapon(&self) -> Option<&Weapon<S>> {
		self.weapons.get(self.core.active)
	}

	pub fn weapons(&self) -> &[Weapon<S>] {
		&self.weapons
	}

	pub fn set_enabled(&mut self, enabled: bool) {
		self.core.enabled = enabled;
	}

	pub fn set_input(&mut self, flags: InputFlags) {
		self.core.input.flags = flags.0;
	}

	pub fn load_def(base: &Path, path: &str) -> Option<WeaponDef> {
		let bytes = match std::fs::read(base.join(path)) {
			Ok(bytes) => bytes,
			Err(_) => {
				raptor_core::log_error!("Could not read weapon config '{path}'");
				return None;
			}
		};

		let constants = base.join(CONSTANTS_PATH);
		let constants = constants.to_string_lossy().into_owned();

		match WeaponDef::parse(&bytes, Some(constants.as_bytes()), &mut FsHost) {
			Ok(def) => Some(def),
			Err(WeaponError::NoScript) => {
				raptor_core::log_error!("Weapon config '{path}' has no Script entry");
				None
			}
			Err(WeaponError::Parse) => {
				raptor_core::log_error!("Could not parse weapon config '{path}'");
				None
			}
		}
	}

	pub fn discover(base: &Path) -> Vec<WeaponDef> {
		let directory = base.join(WEAPON_DIRECTORY);

		let Ok(entries) = std::fs::read_dir(&directory) else {
			raptor_core::log_warn!("Weapon directory '{}' does not exist", directory.display());
			return Vec::new();
		};

		let mut files: Vec<String> = entries
			.flatten()
			.filter_map(|entry| entry.file_name().into_string().ok())
			.filter(|name| name.ends_with(".conf"))
			.collect();

		files.sort();

		let mut defs = Vec::new();

		for file in files {
			if defs.len() >= MAX_WEAPONS {
				raptor_core::log_warn!("Too many weapons, ignoring '{file}'");
				break;
			}

			if let Some(def) = Self::load_def(base, &format!("{WEAPON_DIRECTORY}/{file}")) {
				defs.push(def);
			}
		}

		defs
	}

	pub fn install(&mut self, env: &mut dyn WeaponEnv, weapons: Vec<(WeaponDef, S)>) {
		self.weapons.clear();
		self.core.active = 0;
		self.core.pending = 0;
		self.core.switching = false;

		for (def, script) in weapons {
			let state = ScriptState {
				fire_mode: def.def.default_mode,
				magazine: def.def.magazine_size,
				reserve: def.def.reserve_start,
				..ScriptState::default()
			};

			let weapon = Weapon {
				name: def.name,
				slot: def.slot,
				script_path: def.script,
				idle_anim: def.idle_anim,
				fire_anim: def.fire_anim,
				reload_anim: def.reload_anim,
				view_kick_degrees: def.view_kick,
				def: def.def,
				state,
				script,
			};

			let at = self
				.weapons
				.iter()
				.rposition(|other| other.slot <= weapon.slot)
				.map_or(0, |index| index + 1);

			self.weapons.insert(at, weapon);
		}

		for index in 0..self.weapons.len() {
			let (weapons, core) = (&mut self.weapons, &mut self.core);
			let weapon = &mut weapons[index];

			let mut ctx = Ctx {
				env,
				core,
				name: &weapon.name,
				view_kick: weapon.view_kick_degrees,
			};

			weapon.script.init(&weapon.def, &mut weapon.state, &mut ctx);

			raptor_core::log_info!(
				"Loaded weapon '{}' (slot {}, {}/{} rounds)",
				weapon.name,
				weapon.slot,
				weapon.state.magazine,
				weapon.state.reserve
			);
		}

		if self.weapons.is_empty() {
			return;
		}

		self.apply_animations(env, 0);
		self.equip(env, 0);
	}

	fn apply_animations(&self, env: &mut dyn WeaponEnv, index: usize) {
		let weapon = &self.weapons[index];

		env.set_view_model_animations(&weapon.idle_anim, &weapon.fire_anim, &weapon.reload_anim);
		env.set_recoil_recovery(weapon.def.recoil_recovery);
	}

	fn equip(&mut self, env: &mut dyn WeaponEnv, index: usize) -> bool {
		let (weapons, core) = (&mut self.weapons, &mut self.core);
		let weapon = &mut weapons[index];

		let mut ctx = Ctx {
			env,
			core,
			name: &weapon.name,
			view_kick: weapon.view_kick_degrees,
		};

		weapon
			.script
			.equip(&weapon.def, &mut weapon.state, &mut ctx)
	}

	pub fn on_scripts_reloaded(&mut self) {
		for weapon in &mut self.weapons {
			weapon.script.rebind();
		}
	}

	pub fn select_weapon(&mut self, env: &mut dyn WeaponEnv, index: usize) {
		let _ = env;

		if index >= self.weapons.len() {
			return;
		}

		if self.core.switching {
			self.core.pending = index;
			return;
		}

		if index == self.core.active {
			return;
		}

		self.core.pending = index;
		self.core.switching = true;

		let active = self.core.active;

		let (weapons, core) = (&mut self.weapons, &mut self.core);
		let weapon = &mut weapons[active];

		let mut ctx = Ctx {
			env,
			core,
			name: &weapon.name,
			view_kick: weapon.view_kick_degrees,
		};

		if !weapon
			.script
			.holster(&weapon.def, &mut weapon.state, &mut ctx)
		{
			weapon.state.set_phase(Phase::Lowered);
		}
	}

	pub fn switch_to_next_weapon(&mut self, env: &mut dyn WeaponEnv) {
		if self.weapons.len() < 2 {
			return;
		}

		let base = if self.core.switching {
			self.core.pending
		} else {
			self.core.active
		};

		self.select_weapon(env, (base + 1) % self.weapons.len());
	}

	fn update_switching(&mut self, env: &mut dyn WeaponEnv) {
		if !self.core.switching || self.weapons[self.core.active].state.phase() != Phase::Lowered {
			return;
		}

		self.core.active = self.core.pending;
		self.core.switching = false;

		let active = self.core.active;

		self.apply_animations(env, active);

		if !self.equip(env, active) {
			self.weapons[active].state.set_phase(Phase::Ready);
		}
	}

	fn update_holster_amount(&self, env: &mut dyn WeaponEnv) {
		let state = &self.weapons[self.core.active].state;

		let progress = (state.timer / state.timer_total).clamp(0.0, 1.0);

		let amount = match state.phase() {
			Phase::Lowering => 1.0 - progress,
			Phase::Lowered => 1.0,
			Phase::Raising => progress,
			_ => 0.0,
		};

		env.set_view_model_holster(amount * amount * (3.0 - 2.0 * amount));
	}

	pub fn update(&mut self, env: &mut dyn WeaponEnv, delta_time: f32) {
		if self.weapons.is_empty() {
			return;
		}

		if !self.core.enabled {
			self.core.fire_latched = false;
			return;
		}

		let flags = InputFlags(self.core.input.flags);

		if flags.has(FIRE_PRESSED) {
			self.core.fire_latched = true;
		}

		if !flags.has(FIRE_HELD) {
			self.core.fire_latched = false;
		}

		if flags.has(SWITCH_WEAPON) {
			self.switch_to_next_weapon(env);
		}

		let velocity = env.player_velocity();
		let horizontal_speed = (velocity[0] * velocity[0] + velocity[2] * velocity[2]).sqrt();

		let held = flags.has(FIRE_HELD) && self.core.fire_latched;

		let mut flags = InputFlags(self.core.input.flags).with(FIRE_HELD, held);

		if !env.player_grounded() && !env.player_fly_mode() {
			flags = flags.with(AIRBORNE, true);
		}

		self.core.input.flags = flags.0;
		self.core.input.move_amount = (horizontal_speed / SPRINT_SPEED).clamp(0.0, 1.0);

		let active = self.core.active;
		let input = self.core.input;

		{
			let (weapons, core) = (&mut self.weapons, &mut self.core);
			let weapon = &mut weapons[active];

			let mut ctx = Ctx {
				env,
				core,
				name: &weapon.name,
				view_kick: weapon.view_kick_degrees,
			};

			weapon
				.script
				.update(&weapon.def, &mut weapon.state, &input, delta_time, &mut ctx);
		}

		self.update_switching(env);
		self.update_holster_amount(env);
	}

	pub fn handle_event(&mut self, env: &mut dyn WeaponEnv, event: Event) {
		if self.weapons.is_empty() {
			return;
		}

		let (weapons, core) = (&mut self.weapons, &mut self.core);
		let weapon = &weapons[core.active];

		let mut ctx = Ctx {
			env,
			core,
			name: &weapon.name,
			view_kick: weapon.view_kick_degrees,
		};

		ctx.event(event);
	}

	pub fn trace(&mut self, env: &mut dyn WeaponEnv, yaw: f32, pitch: f32, range: f32) -> f32 {
		let Some(weapon) = self.weapons.get(self.core.active) else {
			return -1.0;
		};

		let mut ctx = Ctx {
			env,
			core: &mut self.core,
			name: &weapon.name,
			view_kick: weapon.view_kick_degrees,
		};

		ctx.trace(yaw, pitch, range)
	}

	pub fn apply_hit(&mut self, env: &mut dyn WeaponEnv, damage: f32, force: f32, decal: bool) {
		let Some(weapon) = self.weapons.get(self.core.active) else {
			return;
		};

		let mut ctx = Ctx {
			env,
			core: &mut self.core,
			name: &weapon.name,
			view_kick: weapon.view_kick_degrees,
		};

		ctx.apply_hit(damage, force, decal);
	}

	pub fn kick(&mut self, env: &mut dyn WeaponEnv, pitch: f32, yaw: f32) {
		env.add_recoil(pitch, yaw);
	}

	pub fn render_hud(&self, env: &mut dyn WeaponEnv) {
		if !self.core.enabled {
			return;
		}

		let Some(weapon) = self.weapons.get(self.core.active) else {
			return;
		};

		let state = &weapon.state;

		let window = env.window_size();
		let glyph = env.glyph_size();

		let right_edge = window[0] as f32 - 32.0;
		let bottom_edge = 64.0;

		let white = pack(255, 255, 255);
		let amber = pack(255, 190, 40);
		let red = pack(255, 70, 50);
		let grey = pack(170, 170, 170);

		let ammo_text = format!("{:02} / {:03}", state.magazine, state.reserve);
		let mode_text = format!(
			"{} [{}]",
			weapon.name,
			FireMode::from_raw(state.fire_mode).label()
		);

		let ammo_width = ammo_text.len() as f32 * glyph[0];
		let mode_width = mode_text.len() as f32 * glyph[0];

		let ammo_color = if state.magazine == 0 {
			red
		} else if state.magazine * 4 <= weapon.def.magazine_size {
			amber
		} else {
			white
		};

		let ammo_y = bottom_edge - glyph[1];

		env.draw_text(
			&ammo_text,
			[right_edge - ammo_width, ammo_y],
			1.0,
			ammo_color,
		);

		let mode_y = ammo_y - glyph[1] - 6.0;

		env.draw_text(&mode_text, [right_edge - mode_width, mode_y], 1.0, grey);

		let status = if state.phase() == Phase::Reloading {
			Some("reloading")
		} else if state.magazine == 0 && state.reserve == 0 {
			Some("no ammo")
		} else if state.magazine == 0 {
			Some("reload")
		} else {
			None
		};

		if let Some(status) = status {
			let width = status.len() as f32 * glyph[0];

			env.draw_text(
				status,
				[right_edge - width, mode_y - glyph[1] - 6.0],
				1.0,
				amber,
			);
		}
	}
}

impl<S: WeaponScript> Default for WeaponSystem<S> {
	fn default() -> Self {
		Self::new()
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::env::CameraBasis;

	#[derive(Default)]
	struct Env {
		holster: f32,
		recoil: Vec<(f32, f32)>,
		fire_kicks: Vec<f32>,
		reloads: u32,
		bullet_holes: Vec<[f32; 3]>,
		blood: Vec<f32>,
		pushes: Vec<(u32, [f32; 3])>,
		texts: Vec<String>,
		dynamic: bool,
		bleeds: bool,
		floor_y: f32,
		animations: Vec<String>,
	}

	impl WeaponEnv for Env {
		fn camera(&self) -> CameraBasis {
			CameraBasis {
				position: [0.0, 1.0, 0.0],
				forward: [0.0, 0.0, 1.0],
				right: [1.0, 0.0, 0.0],
				up: [0.0, 1.0, 0.0],
			}
		}

		fn player_velocity(&self) -> [f32; 3] {
			[3.0, 0.0, 0.0]
		}

		fn player_grounded(&self) -> bool {
			false
		}

		fn player_fly_mode(&self) -> bool {
			false
		}

		fn set_view_model_animations(&mut self, idle: &str, _fire: &str, _reload: &str) {
			self.animations.push(idle.to_owned());
		}

		fn set_recoil_recovery(&mut self, _rate: f32) {}

		fn set_view_model_holster(&mut self, amount: f32) {
			self.holster = amount;
		}

		fn do_fire_animation(&mut self, kick_degrees: f32) {
			self.fire_kicks.push(kick_degrees);
		}

		fn do_reload_animation(&mut self) {
			self.reloads += 1;
		}

		fn add_recoil(&mut self, pitch: f32, yaw: f32) {
			self.recoil.push((pitch, yaw));
		}

		fn raycast(
			&self,
			origin: [f32; 3],
			direction: [f32; 3],
			ignore: Option<u32>,
		) -> Option<SurfaceHit> {
			if ignore.is_some() {
				return (direction[1] < 0.0).then_some(SurfaceHit {
					body: 9,
					point: [origin[0], self.floor_y, origin[2]],
					normal: [0.0, 1.0, 0.0],
				});
			}

			Some(SurfaceHit {
				body: 5,
				point: [
					origin[0] + direction[0],
					origin[1] + direction[1],
					origin[2] + direction[2],
				],
				normal: [0.0, 0.0, -1.0],
			})
		}

		fn body_is_dynamic(&self, _body: u32) -> bool {
			self.dynamic
		}

		fn body_bleeds(&self, _body: u32) -> bool {
			self.bleeds
		}

		fn push_body(&mut self, body: u32, impulse: [f32; 3]) {
			self.pushes.push((body, impulse));
		}

		fn add_bullet_hole(&mut self, point: [f32; 3], _normal: [f32; 3]) {
			self.bullet_holes.push(point);
		}

		fn add_blood_splat(&mut self, _point: [f32; 3], _normal: [f32; 3], size: f32) {
			self.blood.push(size);
		}

		fn random_unit(&mut self) -> f32 {
			0.5
		}

		fn window_size(&self) -> [u32; 2] {
			[800, 600]
		}

		fn glyph_size(&self) -> [f32; 2] {
			[6.0, 12.0]
		}

		fn draw_text(&mut self, text: &str, _position: [f32; 2], _scale: f32, _color: u32) {
			self.texts.push(text.to_owned());
		}

		fn debug_enabled(&self) -> bool {
			false
		}

		fn log(&mut self, _message: &str) {}
	}

	#[derive(Default)]
	struct Script {
		has_holster: bool,
		updates: u32,
		last_flags: i32,
		last_move: f32,
	}

	impl WeaponScript for Script {
		fn init(&mut self, _def: &ScriptDef, _state: &mut ScriptState, _host: &mut dyn ScriptHost) {
		}

		fn equip(
			&mut self,
			_def: &ScriptDef,
			state: &mut ScriptState,
			_host: &mut dyn ScriptHost,
		) -> bool {
			state.set_phase(Phase::Raising);
			true
		}

		fn holster(
			&mut self,
			_def: &ScriptDef,
			state: &mut ScriptState,
			_host: &mut dyn ScriptHost,
		) -> bool {
			if self.has_holster {
				state.set_phase(Phase::Lowering);
			}

			self.has_holster
		}

		fn update(
			&mut self,
			_def: &ScriptDef,
			_state: &mut ScriptState,
			input: &ScriptInput,
			_delta_time: f32,
			_host: &mut dyn ScriptHost,
		) -> bool {
			self.updates += 1;
			self.last_flags = input.flags;
			self.last_move = input.move_amount;
			true
		}

		fn rebind(&mut self) {}
	}

	fn system(count: usize) -> WeaponSystem<Script> {
		let mut system = WeaponSystem::new();

		for index in 0..count {
			system.weapons.push(Weapon {
				name: format!("W{index}"),
				slot: index as i32,
				script_path: String::new(),
				idle_anim: format!("idle{index}"),
				fire_anim: String::new(),
				reload_anim: String::new(),
				view_kick_degrees: 2.0,
				def: ScriptDef {
					magazine_size: 30,
					..ScriptDef::default()
				},
				state: ScriptState {
					magazine: 30,
					reserve: 90,
					fire_mode: 1,
					..ScriptState::default()
				},
				script: Script::default(),
			});
		}

		system
	}

	#[test]
	fn the_script_sees_airborne_and_how_fast_the_player_moves() {
		let mut env = Env::default();
		let mut system = system(1);

		system.update(&mut env, 0.016);

		let script = &system.weapons[0].script;

		assert_eq!(script.updates, 1);
		assert!(script.last_flags & AIRBORNE != 0);
		assert!((script.last_move - 0.6).abs() < 1e-5);
	}

	#[test]
	fn a_weapon_without_a_holster_function_is_lowered_at_once_and_switching_follows() {
		let mut env = Env::default();
		let mut system = system(2);

		system.select_weapon(&mut env, 1);

		assert_eq!(system.weapons[0].state.phase(), Phase::Lowered);

		system.update(&mut env, 0.016);

		assert_eq!(system.core.active, 1);
		assert_eq!(system.weapons[1].state.phase(), Phase::Raising);
		assert_eq!(env.animations.last().map(String::as_str), Some("idle1"));
	}

	#[test]
	fn switching_waits_for_the_holster_to_finish() {
		let mut env = Env::default();
		let mut system = system(2);

		system.weapons[0].script.has_holster = true;
		system.select_weapon(&mut env, 1);

		assert_eq!(system.weapons[0].state.phase(), Phase::Lowering);

		system.update(&mut env, 0.016);

		assert_eq!(system.core.active, 0);

		system.weapons[0].state.set_phase(Phase::Lowered);
		system.update(&mut env, 0.016);

		assert_eq!(system.core.active, 1);
	}

	#[test]
	fn a_hit_on_a_wall_leaves_a_bullet_hole_and_one_on_a_body_pushes_it() {
		let mut env = Env::default();
		let mut system = system(1);

		let distance = system.trace(&mut env, 0.0, 0.0, 10.0);

		assert!((distance - 10.0_f32.hypot(0.0)).abs() < 1e-4);

		system.apply_hit(&mut env, 10.0, 5.0, true);

		assert_eq!(env.bullet_holes.len(), 1);
		assert!(env.pushes.is_empty());

		env.dynamic = true;

		system.trace(&mut env, 0.0, 0.0, 10.0);
		system.apply_hit(&mut env, 10.0, 5.0, true);

		assert_eq!(env.pushes.len(), 1);
		assert_eq!(env.pushes[0].1, [0.0, 0.0, 5.0]);
	}

	#[test]
	fn bleeding_bodies_splatter_blood_in_place_of_bullet_holes() {
		let mut env = Env {
			bleeds: true,
			dynamic: true,
			floor_y: 0.0,
			..Env::default()
		};

		let mut system = system(1);

		system.trace(&mut env, 0.0, 0.0, 10.0);
		system.apply_hit(&mut env, 10.0, 0.0, true);

		assert!(env.bullet_holes.is_empty());
		assert!(!env.blood.is_empty());
	}

	#[test]
	fn hitting_nothing_does_nothing() {
		let mut env = Env::default();
		let mut system = system(1);

		system.core.last_hit = None;
		system.apply_hit(&mut env, 1.0, 1.0, true);

		assert!(env.bullet_holes.is_empty());
	}

	#[test]
	fn events_drive_the_view_model() {
		let mut env = Env::default();
		let mut system = system(1);

		system.handle_event(&mut env, Event::Fire);
		system.handle_event(&mut env, Event::ReloadBegin);
		system.handle_event(&mut env, Event::DryFire);

		assert_eq!(env.fire_kicks, vec![2.0]);
		assert_eq!(env.reloads, 1);
	}

	#[test]
	fn the_hud_turns_red_on_an_empty_magazine_and_says_to_reload() {
		let mut env = Env::default();
		let mut system = system(1);

		system.weapons[0].state.magazine = 0;
		system.render_hud(&mut env);

		assert!(env.texts.contains(&"00 / 090".to_owned()));
		assert!(env.texts.contains(&"reload".to_owned()));
	}

	#[test]
	fn a_disabled_system_does_not_update() {
		let mut env = Env::default();
		let mut system = system(1);

		system.set_enabled(false);
		system.update(&mut env, 0.016);

		assert_eq!(system.weapons[0].script.updates, 0);
	}
}

impl WeaponScript for Box<dyn WeaponScript> {
	fn init(&mut self, def: &ScriptDef, state: &mut ScriptState, host: &mut dyn ScriptHost) {
		(**self).init(def, state, host);
	}

	fn equip(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		host: &mut dyn ScriptHost,
	) -> bool {
		(**self).equip(def, state, host)
	}

	fn holster(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		host: &mut dyn ScriptHost,
	) -> bool {
		(**self).holster(def, state, host)
	}

	fn update(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		input: &ScriptInput,
		delta_time: f32,
		host: &mut dyn ScriptHost,
	) -> bool {
		(**self).update(def, state, input, delta_time, host)
	}

	fn rebind(&mut self) {
		(**self).rebind();
	}
}
