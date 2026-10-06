use crate::backend::{Backend, CharacterSpec};
use crate::layers::Layer;
use crate::world::PhysicsWorld;

/// How much stronger than the world's gravity the player falls
pub const GRAVITY_SCALE: f32 = 1.5;

/// The body the player moves around in. Movement is asked for each frame and the character is
/// moved through the world by the physics engine.
pub struct Character
{
	handle: u32,
	movement: [f32; 3],
	grounded: bool,
	pub disable_gravity: bool,
	collision_enabled: bool,
}

impl Character
{
	pub fn new<B: Backend>(world: &mut PhysicsWorld<B>, spec: &CharacterSpec) -> Option<Self>
	{
		let handle = world.backend_mut().create_character(spec)?;

		Some(Self {
			handle,
			movement: [0.0; 3],
			grounded: false,
			disable_gravity: false,
			collision_enabled: true,
		})
	}

	pub fn destroy<B: Backend>(self, world: &mut PhysicsWorld<B>)
	{
		world.backend_mut().destroy_character(self.handle);
	}

	pub fn teleport<B: Backend>(&self, world: &mut PhysicsWorld<B>, position: [f32; 3])
	{
		world
			.backend_mut()
			.character_set_position(self.handle, position);
	}

	/// What the character is asked to move by in the next update
	pub fn apply_movement(&mut self, movement: [f32; 3])
	{
		self.movement = movement;
	}

	/// Turns the character's collision with the world on or off for its movement. The body it has
	/// in the world is always taken off the layers that bullets and grabs hit.
	pub fn set_collision_enabled<B: Backend>(&mut self, world: &mut PhysicsWorld<B>, enabled: bool)
	{
		self.collision_enabled = enabled;

		world
			.backend_mut()
			.character_set_layer(self.handle, Layer::Deactivated);
	}

	pub fn is_grounded(&self) -> bool
	{
		self.grounded
	}

	pub fn position<B: Backend>(&self, world: &PhysicsWorld<B>) -> [f32; 3]
	{
		world.backend().character_position(self.handle)
	}

	pub fn linear_velocity<B: Backend>(&self, world: &PhysicsWorld<B>) -> [f32; 3]
	{
		world.backend().character_linear_velocity(self.handle)
	}

	pub fn ray_bodies<B: Backend>(&self, world: &PhysicsWorld<B>, direction: [f32; 3]) -> Vec<u32>
	{
		world.backend().character_ray_bodies(self.handle, direction)
	}

	/// Works out the velocity the character has this frame from gravity and the movement asked for,
	/// and moves it.
	pub fn update<B: Backend>(&mut self, world: &mut PhysicsWorld<B>, delta_time: f32)
	{
		let backend = world.backend_mut();

		let gravity = backend
			.gravity()
			.map(|value| value * GRAVITY_SCALE * delta_time);

		let mut velocity = [0.0; 3];

		if backend.character_on_ground(self.handle) {
			self.grounded = true;
		} else {
			let current = backend.character_linear_velocity(self.handle);
			let up = backend.character_up(self.handle);

			velocity = [current[0] * up[0], current[1] * up[1], current[2] * up[2]];

			if self.disable_gravity {
				velocity[1] = 0.0;
			} else {
				for axis in 0..3 {
					velocity[axis] += gravity[axis];
				}
			}

			self.grounded = false;
		}

		for axis in 0..3 {
			velocity[axis] += self.movement[axis];
		}

		backend.character_set_linear_velocity(self.handle, velocity);

		let layer = if self.collision_enabled {
			Layer::Dynamic
		} else {
			Layer::Deactivated
		};

		backend.character_update(self.handle, delta_time, gravity, layer);
	}
}

#[cfg(test)]
mod tests
{
	use super::*;
	use crate::backend::{BodySpec, RayHit, ShapeHandle};

	#[derive(Default)]
	struct Fake
	{
		on_ground: bool,
		velocity: [f32; 3],
		set_velocity: Vec<[f32; 3]>,
		updates: Vec<(f32, [f32; 3], Layer)>,
		layer: Option<Layer>,
		position: [f32; 3],
	}

	impl Backend for Fake
	{
		fn make_box(&mut self, _h: [f32; 3], _d: f32, _c: f32) -> Result<ShapeHandle, String>
		{
			Err(String::new())
		}

		fn make_hull(&mut self, _p: &[[f32; 3]], _c: f32, _d: f32) -> Result<ShapeHandle, String>
		{
			Err(String::new())
		}

		fn make_mesh(&mut self, _p: &[[f32; 3]], _t: &[[u32; 3]]) -> Result<ShapeHandle, String>
		{
			Err(String::new())
		}

		fn release_shape(&mut self, _s: ShapeHandle) {}

		fn create_body(&mut self, _s: &BodySpec) -> Option<u32>
		{
			None
		}

		fn add_body(&mut self, _b: u32, _a: bool) {}

		fn remove_body(&mut self, _b: u32) {}

		fn destroy_body(&mut self, _b: u32) {}

		fn set_position_rotation(&mut self, _b: u32, _p: [f32; 3], _r: [f32; 4], _a: bool) {}

		fn position_rotation(&self, _b: u32) -> ([f32; 3], [f32; 4])
		{
			([0.0; 3], [0.0, 0.0, 0.0, 1.0])
		}

		fn cast_ray(&self, _o: [f32; 3], _d: [f32; 3], _i: Option<u32>) -> Option<RayHit>
		{
			None
		}

		fn cast_ray_all(&self, _o: [f32; 3], _d: [f32; 3]) -> Vec<(u32, f32)>
		{
			Vec::new()
		}

		fn surface_normal_along_ray(&self, _b: u32, _o: [f32; 3], _d: [f32; 3])
		-> Option<[f32; 3]>
		{
			None
		}

		fn step(&mut self, _t: f32, _s: u32) {}

		fn optimize_broad_phase(&mut self) {}

		fn is_dynamic(&self, _b: u32) -> bool
		{
			false
		}

		fn body_bounds(&self, _b: u32) -> ([f32; 3], [f32; 3])
		{
			([0.0; 3], [0.0; 3])
		}

		fn is_added(&self, _b: u32) -> bool
		{
			false
		}

		fn is_active(&self, _b: u32) -> bool
		{
			false
		}

		fn activate(&mut self, _b: u32) {}

		fn deactivate(&mut self, _b: u32) {}

		fn add_impulse(&mut self, _b: u32, _i: [f32; 3]) {}

		fn center_of_mass(&self, _b: u32) -> ([f32; 3], [f32; 4])
		{
			([0.0; 3], [0.0, 0.0, 0.0, 1.0])
		}

		fn angular_velocity(&self, _b: u32) -> [f32; 3]
		{
			[0.0; 3]
		}

		fn set_velocities(&mut self, _b: u32, _l: [f32; 3], _a: [f32; 3]) {}

		fn create_character(&mut self, _spec: &CharacterSpec) -> Option<u32>
		{
			Some(1)
		}

		fn character_set_position(&mut self, _c: u32, position: [f32; 3])
		{
			self.position = position;
		}

		fn character_position(&self, _c: u32) -> [f32; 3]
		{
			self.position
		}

		fn character_linear_velocity(&self, _c: u32) -> [f32; 3]
		{
			self.velocity
		}

		fn character_set_linear_velocity(&mut self, _c: u32, velocity: [f32; 3])
		{
			self.set_velocity.push(velocity);
		}

		fn character_on_ground(&self, _c: u32) -> bool
		{
			self.on_ground
		}

		fn character_update(&mut self, _c: u32, dt: f32, gravity: [f32; 3], layer: Layer)
		{
			self.updates.push((dt, gravity, layer));
		}

		fn character_set_layer(&mut self, _c: u32, layer: Layer)
		{
			self.layer = Some(layer);
		}
	}

	fn spec() -> CharacterSpec
	{
		CharacterSpec {
			standing_height: 1.7,
			radius: 0.3,
			mass: 80.0,
			max_strength: 100.0,
			max_slope_angle: 0.8,
		}
	}

	fn setup() -> (PhysicsWorld<Fake>, Character)
	{
		let mut world = PhysicsWorld::new(Fake::default());
		let character = Character::new(&mut world, &spec()).unwrap();

		(world, character)
	}

	#[test]
	fn on_the_ground_the_character_only_moves_by_what_it_is_asked_to()
	{
		let (mut world, mut character) = setup();

		world.backend_mut().on_ground = true;
		character.apply_movement([1.0, 0.0, 2.0]);
		character.update(&mut world, 0.01);

		assert!(character.is_grounded());
		assert_eq!(world.backend().set_velocity, vec![[1.0, 0.0, 2.0]]);
		assert_eq!(world.backend().updates[0].2, Layer::Dynamic);
	}

	#[test]
	fn in_the_air_gravity_is_added_to_the_vertical_speed()
	{
		let (mut world, mut character) = setup();

		world.backend_mut().velocity = [5.0, -1.0, 5.0];
		character.update(&mut world, 0.1);

		assert!(!character.is_grounded());

		let velocity = world.backend().set_velocity[0];

		assert_eq!(velocity[0], 0.0);
		assert!((velocity[1] - (-1.0 + -9.81 * GRAVITY_SCALE * 0.1)).abs() < 1e-5);
	}

	#[test]
	fn with_gravity_off_the_vertical_speed_is_kept_at_zero()
	{
		let (mut world, mut character) = setup();

		character.disable_gravity = true;
		world.backend_mut().velocity = [0.0, -3.0, 0.0];
		character.apply_movement([0.0, 2.0, 0.0]);
		character.update(&mut world, 0.1);

		assert_eq!(world.backend().set_velocity[0], [0.0, 2.0, 0.0]);
	}

	#[test]
	fn turning_collision_off_moves_the_character_on_the_deactivated_layer()
	{
		let (mut world, mut character) = setup();

		character.set_collision_enabled(&mut world, false);
		character.update(&mut world, 0.01);

		assert_eq!(world.backend().layer, Some(Layer::Deactivated));
		assert_eq!(world.backend().updates[0].2, Layer::Deactivated);
	}

	#[test]
	fn teleporting_sets_the_position()
	{
		let (mut world, character) = setup();

		character.teleport(&mut world, [1.0, 2.0, 3.0]);

		assert_eq!(character.position(&world), [1.0, 2.0, 3.0]);
	}
}
