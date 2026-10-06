use raptor_entity::{CameraCore, ProjectionKind};
use raptor_level::access::{FsHost, Reader, find};
use raptor_math::Vec3f;
use raptor_physics::character::Character;
use raptor_physics::{Backend, CharacterSpec, PhysicsWorld};
use raptor_player::PlayerState;

pub const STANDING_HEIGHT: f32 = 1.72;
pub const WALKING_FOV: f32 = 80.0;
pub const MAX_SLOPE_ANGLE: f32 = std::f32::consts::FRAC_PI_4;

pub const DEFAULT_IDLE_ANIMATION: &str = "IDLE";
pub const DEFAULT_FIRE_ANIMATION: &str = "Armature|Fire";
pub const DEFAULT_RELOAD_ANIMATION: &str = "Armature|ReloadClip";
pub const DEFAULT_VIEW_KICK_DEGREES: f32 = 2.5;
pub const DEFAULT_VIEW_KICKBACK: f32 = 0.025;

pub const PLAYER_CONFIG_PATH: &str = "RaptorData/Data/Player.conf";

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlayerConfig {
	pub collider_radius: f32,
	pub mass: f32,
	pub strength: f32,
}

impl Default for PlayerConfig {
	fn default() -> Self {
		Self {
			collider_radius: 0.3,
			mass: 80.0,
			strength: 100.0,
		}
	}
}

impl PlayerConfig {
	pub fn parse(data: &[u8], constants: Option<&[u8]>) -> Self {
		let mut host = FsHost;
		let parsed = raptor_config::parse(data, constants, b".conf", &mut host);

		let mut config = Self::default();
		let mut reader = Reader::new(&mut host);

		let mut read = |name: &str, slot: &mut f32| {
			if let Some(entry) = find(&parsed.entries, name) {
				*slot = reader.float_of(&entry.value);
			}
		};

		read("ColliderRadius", &mut config.collider_radius);
		read("Mass", &mut config.mass);
		read("Strength", &mut config.strength);

		config
	}
}

impl ViewModelRig for raptor_anim::Skeleton {
	fn find_animation(&self, name: &str) -> Option<u32> {
		let animation = raptor_anim::Skeleton::find_animation(self, name);

		(animation != raptor_anim::skeleton::NO_ANIMATION).then_some(animation)
	}

	fn set_rest_animation(&mut self, animation: u32) {
		raptor_anim::Skeleton::set_rest_animation(self, animation, 1.0);
	}

	fn active_animation(&self) -> Option<u32> {
		self.active_playback().map(|playback| playback.animation)
	}

	fn push_animation(&mut self, animation: u32) {
		raptor_anim::Skeleton::push_animation(self, animation, raptor_anim::AnimationEnd::Pop, 1.0);
	}

	fn pop_animation(&mut self) {
		raptor_anim::Skeleton::pop_animation(self);
	}
}

/// The animations of the view model's skeleton that the player plays.
pub trait ViewModelRig {
	fn find_animation(&self, name: &str) -> Option<u32>;
	fn set_rest_animation(&mut self, animation: u32);
	fn active_animation(&self) -> Option<u32>;
	fn push_animation(&mut self, animation: u32);
	fn pop_animation(&mut self);
}

/// The first person player: the camera, the character that is moved through the world, and the
/// view motion that follows the camera.
pub struct Player {
	pub state: PlayerState,
	pub camera: CameraCore,
	pub character: Option<Character>,
	pub view_model: Option<u32>,
	pub idle_animation: String,
	pub fire_animation: String,
	pub reload_animation: String,
	gravity_disabled: bool,
}

impl Default for Player {
	fn default() -> Self {
		Self::new()
	}
}

impl Player {
	pub fn new() -> Self {
		Self {
			state: PlayerState::default(),
			camera: CameraCore::new(ProjectionKind::Perspective),
			character: None,
			view_model: None,
			idle_animation: DEFAULT_IDLE_ANIMATION.to_owned(),
			fire_animation: DEFAULT_FIRE_ANIMATION.to_owned(),
			reload_animation: DEFAULT_RELOAD_ANIMATION.to_owned(),
			gravity_disabled: false,
		}
	}

	pub fn create<B: Backend>(
		&mut self,
		world: &mut PhysicsWorld<B>,
		config: &PlayerConfig,
		aspect_ratio: f32,
	) -> bool {
		self.camera.aspect = aspect_ratio;
		self.camera.fov_rad = WALKING_FOV.to_radians();
		self.camera.update_projection = 1;

		self.character = Character::new(
			world,
			&CharacterSpec {
				standing_height: STANDING_HEIGHT,
				radius: config.collider_radius,
				mass: config.mass,
				max_strength: config.strength,
				max_slope_angle: MAX_SLOPE_ANGLE,
			},
		);

		self.state.camera_offset[1] = STANDING_HEIGHT;

		self.character.is_some()
	}

	pub fn destroy<B: Backend>(&mut self, world: &mut PhysicsWorld<B>) {
		if let Some(character) = self.character.take() {
			character.destroy(world);
		}
	}

	pub fn position(&self) -> [f32; 3] {
		[
			self.state.position[0],
			self.state.position[1],
			self.state.position[2],
		]
	}

	pub fn is_fly_mode(&self) -> bool {
		self.gravity_disabled
	}

	pub fn is_grounded(&self) -> bool {
		self.character.as_ref().is_some_and(Character::is_grounded)
	}

	pub fn linear_velocity<B: Backend>(&self, world: &PhysicsWorld<B>) -> [f32; 3] {
		self.character
			.as_ref()
			.map_or([0.0; 3], |character| character.linear_velocity(world))
	}

	fn sync_position<B: Backend>(&mut self, world: &PhysicsWorld<B>) {
		if let Some(character) = &self.character {
			let position = character.position(world);

			self.state.position = [position[0], position[1], position[2], 0.0];
		}
	}

	pub fn teleport_to<B: Backend>(&mut self, world: &mut PhysicsWorld<B>, position: [f32; 3]) {
		self.state.position = [position[0], position[1], position[2], 0.0];

		if let Some(character) = &self.character {
			character.teleport(world, position);
		}
	}

	pub fn teleport_by<B: Backend>(&mut self, world: &mut PhysicsWorld<B>, offset: [f32; 3]) {
		self.sync_position(world);

		let position = self.position();

		self.teleport_to(
			world,
			[
				position[0] + offset[0],
				position[1] + offset[1],
				position[2] + offset[2],
			],
		);
	}

	pub fn move_by(&mut self, by: [f32; 3]) {
		self.state.move_by(Vec3f::new(by[0], by[1], by[2]));
	}

	pub fn rotate_head(&mut self, yaw: f32, pitch: f32) {
		self.state.rotate_head(&mut self.camera, yaw, pitch);
	}

	pub fn jump(&mut self) {
		let grounded = self.is_grounded();

		self.state.jump(grounded);
	}

	pub fn set_fly_mode(&mut self, value: bool) {
		self.state.set_fly_mode(value);

		self.gravity_disabled = value;

		if let Some(character) = &mut self.character {
			character.disable_gravity = value;
		}
	}

	pub fn move_input(&mut self, delta_time: f64, offset: [f32; 3]) {
		let force = self.state.movement_force(delta_time, offset);

		if let Some(character) = &mut self.character {
			character.apply_movement(force);
		}
	}

	pub fn set_view_model_holster(&mut self, amount: f32) {
		self.state.holster = amount;
	}

	pub fn set_recoil_recovery(&mut self, rate_per_second: f32) {
		self.state.recoil.recovery = rate_per_second;
	}

	pub fn add_recoil(&mut self, pitch: f32, yaw: f32) {
		self.state.recoil.add(pitch, yaw);
	}

	pub fn set_view_model_animations(
		&mut self,
		rig: Option<&mut dyn ViewModelRig>,
		idle: &str,
		fire: &str,
		reload: &str,
	) {
		self.idle_animation = idle.to_owned();
		self.fire_animation = fire.to_owned();
		self.reload_animation = reload.to_owned();

		if let Some(rig) = rig
			&& let Some(animation) = rig.find_animation(&self.idle_animation)
		{
			rig.set_rest_animation(animation);
		}
	}

	pub fn do_fire_animation(
		&mut self,
		rig: Option<&mut dyn ViewModelRig>,
		kick_degrees: f32,
		kickback: f32,
		random_yaw: f32,
		random_roll: f32,
	) {
		self.state
			.kick
			.fire(kick_degrees, kickback, random_yaw, random_roll);

		let Some(rig) = rig else {
			return;
		};

		let Some(fire) = rig.find_animation(&self.fire_animation) else {
			return;
		};

		if rig.active_animation() == Some(fire) {
			rig.pop_animation();
		}

		rig.push_animation(fire);
	}

	pub fn do_reload_animation(&mut self, rig: Option<&mut dyn ViewModelRig>) {
		let Some(rig) = rig else {
			return;
		};

		let Some(reload) = rig.find_animation(&self.reload_animation) else {
			return;
		};

		if rig.active_animation() == Some(reload) {
			return;
		}

		rig.push_animation(reload);
	}

	pub fn update<B: Backend>(
		&mut self,
		world: &mut PhysicsWorld<B>,
		delta_time: f64,
		head_bob_enabled: bool,
	) {
		if let Some(character) = &mut self.character {
			character.update(world, delta_time as f32);
		}

		self.sync_position(world);

		let grounded = self.is_grounded();

		self.state
			.update(&mut self.camera, delta_time, grounded, head_bob_enabled);
	}

	pub fn view_model_pose<B: Backend>(
		&mut self,
		world: &PhysicsWorld<B>,
		delta_time: f32,
	) -> ([f32; 3], [f32; 4]) {
		let velocity = self.linear_velocity(world);

		let pose = self.state.view_model_pose(
			&self.camera,
			Vec3f::new(velocity[0], velocity[1], velocity[2]),
			delta_time,
		);

		(pose.position, pose.rotation)
	}

	pub fn start_sway(&mut self) {
		self.state.start_sway_at(&self.camera);
	}
}

#[cfg(test)]
mod tests {
	use raptor_jolt::JoltBackend;

	use super::*;

	#[derive(Default)]
	struct Rig {
		active: Option<u32>,
		pushed: Vec<u32>,
		popped: u32,
		rest: Option<u32>,
	}

	impl ViewModelRig for Rig {
		fn find_animation(&self, name: &str) -> Option<u32> {
			match name {
				"IDLE" => Some(1),
				"Armature|Fire" => Some(2),
				"Armature|ReloadClip" => Some(3),
				_ => None,
			}
		}

		fn set_rest_animation(&mut self, animation: u32) {
			self.rest = Some(animation);
		}

		fn active_animation(&self) -> Option<u32> {
			self.active
		}

		fn push_animation(&mut self, animation: u32) {
			self.pushed.push(animation);
			self.active = Some(animation);
		}

		fn pop_animation(&mut self) {
			self.popped += 1;
			self.active = None;
		}
	}

	fn world() -> PhysicsWorld<JoltBackend> {
		PhysicsWorld::new(JoltBackend::new().unwrap())
	}

	#[test]
	fn the_config_overrides_the_defaults_it_names() {
		let config = PlayerConfig::parse(b"ColliderRadius = 0.4\nMass = 70.0\n", None);

		assert_eq!(config.collider_radius, 0.4);
		assert_eq!(config.mass, 70.0);
		assert_eq!(config.strength, PlayerConfig::default().strength);
	}

	#[test]
	fn firing_again_restarts_the_fire_animation() {
		let mut player = Player::new();
		let mut rig = Rig::default();

		player.do_fire_animation(Some(&mut rig), 2.5, 0.025, 0.0, 0.0);
		player.do_fire_animation(Some(&mut rig), 2.5, 0.025, 0.0, 0.0);

		assert_eq!(rig.pushed, vec![2, 2]);
		assert_eq!(rig.popped, 1);
	}

	#[test]
	fn reloading_does_not_stack() {
		let mut player = Player::new();
		let mut rig = Rig::default();

		player.do_reload_animation(Some(&mut rig));
		player.do_reload_animation(Some(&mut rig));

		assert_eq!(rig.pushed, vec![3]);
	}

	#[test]
	fn the_player_stands_on_the_floor_and_the_camera_sits_at_eye_height() {
		let mut world = world();

		let floor = world
			.create_box_body(
				None,
				[40.0, 1.0, 40.0],
				raptor_physics::Motion::Static,
				&raptor_physics::BodyProps::default(),
			)
			.unwrap()
			.0;

		world.teleport(floor, [0.0, -0.5, 0.0], [0.0, 0.0, 0.0, 1.0]);

		let mut player = Player::new();

		assert!(player.create(&mut world, &PlayerConfig::default(), 1.0));

		player.teleport_to(&mut world, [0.0, 1.0, 0.0]);

		for _ in 0..180 {
			world.update();
			player.update(&mut world, 1.0 / 60.0, false);
		}

		assert!(player.is_grounded());
		assert!(player.position()[1].abs() < 0.1, "{:?}", player.position());
		assert!((player.camera.position[1] - STANDING_HEIGHT).abs() < 0.15);

		player.destroy(&mut world);
	}

	#[test]
	fn flying_turns_gravity_off_for_the_character() {
		let mut world = world();
		let mut player = Player::new();

		player.create(&mut world, &PlayerConfig::default(), 1.0);
		player.teleport_to(&mut world, [0.0, 10.0, 0.0]);
		player.set_fly_mode(true);

		for _ in 0..60 {
			world.update();
			player.update(&mut world, 1.0 / 60.0, false);
		}

		assert!(player.is_fly_mode());
		assert!(
			(player.position()[1] - 10.0).abs() < 0.05,
			"{:?}",
			player.position()
		);
	}
}
