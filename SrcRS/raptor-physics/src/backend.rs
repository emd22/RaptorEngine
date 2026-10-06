use crate::impacts::ImpactQueue;
use crate::layers::Layer;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ShapeHandle(pub u64);

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RayHit
{
	pub body: u32,
	pub point: [f32; 3],
	/// The surface normal at the hit, which may face away from the ray
	pub normal: [f32; 3],
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BodySpec
{
	pub shape: ShapeHandle,
	pub position: [f32; 3],
	pub rotation: [f32; 4],
	pub dynamic: bool,
	pub layer: Layer,
	pub friction: f32,
	pub restitution: f32,
}

/// What a physics engine has to do for the world. Bodies are known by an id the engine hands out.
pub trait Backend
{
	fn make_box(
		&mut self,
		half_extents: [f32; 3],
		density: f32,
		convex_radius: f32,
	) -> Result<ShapeHandle, String>;

	fn make_hull(
		&mut self,
		points: &[[f32; 3]],
		convex_radius: f32,
		density: f32,
	) -> Result<ShapeHandle, String>;

	fn make_mesh(
		&mut self,
		positions: &[[f32; 3]],
		triangles: &[[u32; 3]],
	) -> Result<ShapeHandle, String>;

	fn release_shape(&mut self, shape: ShapeHandle);

	fn create_body(&mut self, spec: &BodySpec) -> Option<u32>;

	fn add_body(&mut self, body: u32, activate: bool);

	fn remove_body(&mut self, body: u32);

	fn destroy_body(&mut self, body: u32);

	fn set_position_rotation(
		&mut self,
		body: u32,
		position: [f32; 3],
		rotation: [f32; 4],
		activate: bool,
	);

	fn position_rotation(&self, body: u32) -> ([f32; 3], [f32; 4]);

	/// The first body the ray meets, skipping `ignore`
	fn cast_ray(
		&self,
		origin: [f32; 3],
		direction: [f32; 3],
		ignore: Option<u32>,
	) -> Option<RayHit>;

	/// Every body the ray meets with how far along it, in no particular order
	fn cast_ray_all(&self, origin: [f32; 3], direction: [f32; 3]) -> Vec<(u32, f32)>;

	/// The surface normal of a body where the ray from outside meets it first, in world space
	fn surface_normal_along_ray(
		&self,
		body: u32,
		origin: [f32; 3],
		direction: [f32; 3],
	) -> Option<[f32; 3]>;

	/// Advances the simulation by `delta_time` seconds in `collision_steps` steps
	fn step(&mut self, delta_time: f32, collision_steps: u32);

	fn optimize_broad_phase(&mut self);

	fn is_dynamic(&self, body: u32) -> bool;

	/// The box around a body in the world, as its smallest and largest corner
	fn body_bounds(&self, body: u32) -> ([f32; 3], [f32; 3]);

	/// Whether the body is in the simulation, whoever put it there
	fn is_added(&self, body: u32) -> bool;

	fn is_active(&self, body: u32) -> bool;

	fn activate(&mut self, body: u32);

	fn deactivate(&mut self, body: u32);

	fn add_impulse(&mut self, body: u32, impulse: [f32; 3]);

	/// Where the centre of mass is and which way the body faces
	fn center_of_mass(&self, body: u32) -> ([f32; 3], [f32; 4]);

	fn angular_velocity(&self, body: u32) -> [f32; 3];

	fn set_velocities(&mut self, body: u32, linear: [f32; 3], angular: [f32; 3]);

	fn gravity(&self) -> [f32; 3]
	{
		[0.0, -9.81, 0.0]
	}

	/// The hard hits of ragdolls the physics engine has seen, if it reports them
	fn ragdoll_impacts(&self) -> Option<&ImpactQueue>
	{
		None
	}

	fn create_character(&mut self, _spec: &CharacterSpec) -> Option<u32>
	{
		None
	}

	fn destroy_character(&mut self, _character: u32) {}

	fn character_set_position(&mut self, _character: u32, _position: [f32; 3]) {}

	fn character_position(&self, _character: u32) -> [f32; 3]
	{
		[0.0; 3]
	}

	fn character_linear_velocity(&self, _character: u32) -> [f32; 3]
	{
		[0.0; 3]
	}

	fn character_set_linear_velocity(&mut self, _character: u32, _velocity: [f32; 3]) {}

	fn character_on_ground(&self, _character: u32) -> bool
	{
		false
	}

	fn character_up(&self, _character: u32) -> [f32; 3]
	{
		[0.0, 1.0, 0.0]
	}

	/// Moves the character through the world for `delta_time`, colliding with bodies on `layer`'s
	/// terms, walking up stairs and sticking to the floor
	fn character_update(
		&mut self,
		_character: u32,
		_delta_time: f32,
		_gravity: [f32; 3],
		_layer: Layer,
	)
	{
	}

	/// Puts the body the character has inside the world onto a layer
	fn character_set_layer(&mut self, _character: u32, _layer: Layer) {}

	/// The bodies a ray of the direction's length from the character's position passes through
	fn character_ray_bodies(&self, _character: u32, _direction: [f32; 3]) -> Vec<u32>
	{
		Vec::new()
	}

	fn create_ragdoll(&mut self, _parts: &[PartSpec], _serial: u32, _user_data: u64)
	-> Option<u32>
	{
		None
	}

	fn destroy_ragdoll(&mut self, _ragdoll: u32) {}

	fn ragdoll_activate(&mut self, _ragdoll: u32, _velocity: [f32; 3]) {}

	fn ragdoll_is_active(&self, _ragdoll: u32) -> bool
	{
		false
	}

	fn ragdoll_body_pose(&self, _ragdoll: u32, _index: u32) -> ([f32; 3], [f32; 4])
	{
		([0.0; 3], [0.0, 0.0, 0.0, 1.0])
	}
}

/// A character that is moved by being asked to, which a capsule stands in for.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CharacterSpec
{
	pub standing_height: f32,
	pub radius: f32,
	pub mass: f32,
	pub max_strength: f32,
	pub max_slope_angle: f32,
}

/// A body of a ragdoll: a capsule offset and turned within the body, held to its parent by a
/// swing twist joint.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PartSpec
{
	pub position: [f32; 3],
	pub rotation: [f32; 4],
	pub shape_offset: [f32; 3],
	pub shape_rotation: [f32; 4],
	pub half_height: f32,
	pub radius: f32,
	/// The index of the parent part, or none for the root
	pub parent: Option<u32>,
	pub twist_axis: [f32; 3],
	pub plane_axis: [f32; 3],
	pub swing_angle: f32,
	pub twist_angle: f32,
}
