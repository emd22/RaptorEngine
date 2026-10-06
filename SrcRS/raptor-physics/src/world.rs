use std::collections::HashSet;

use crate::backend::{Backend, BodySpec, ShapeHandle};
use crate::layers::Layer;

pub const MIN_DIMENSION: f32 = 0.01;
pub const MIN_HULL_POINTS: usize = 4;

/// The simulation runs at a fixed rate whatever the frame rate is
pub const TIME_STEP: f32 = 1.0 / 60.0;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BodyId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Motion
{
	Static,
	Dynamic,
}

impl Motion
{
	fn layer(self) -> Layer
	{
		match self {
			Self::Static => Layer::Static,
			Self::Dynamic => Layer::Dynamic,
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BodyProps
{
	pub convex_radius: f32,
	pub friction: f32,
	pub restitution: f32,
	/// In kg per cubic metre
	pub density: f32,
}

impl Default for BodyProps
{
	fn default() -> Self
	{
		Self {
			convex_radius: 0.001,
			friction: 0.2,
			restitution: 0.1,
			density: 300.0,
		}
	}
}

#[derive(Debug, PartialEq, Eq)]
pub enum PhysicsError
{
	Shape(String),
	TooFewPoints(usize),
	NotTriangles,
	NoRoomForBody,
	BodyExists,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RayResult
{
	pub body: BodyId,
	pub point: [f32; 3],
	/// Facing back towards the ray
	pub normal: [f32; 3],
}

pub struct PhysicsWorld<B: Backend>
{
	backend: B,
	paused: bool,
	in_world: HashSet<u32>,
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32
{
	a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

/// Rotates a vector by the inverse of a unit quaternion (x, y, z, w).
fn rotate_by_inverse(rotation: [f32; 4], vector: [f32; 3]) -> [f32; 3]
{
	let (x, y, z, w) = (-rotation[0], -rotation[1], -rotation[2], rotation[3]);

	let t = [
		2.0 * (y * vector[2] - z * vector[1]),
		2.0 * (z * vector[0] - x * vector[2]),
		2.0 * (x * vector[1] - y * vector[0]),
	];

	[
		vector[0] + w * t[0] + (y * t[2] - z * t[1]),
		vector[1] + w * t[1] + (z * t[0] - x * t[2]),
		vector[2] + w * t[2] + (x * t[1] - y * t[0]),
	]
}

/// Rotates a vector by a unit quaternion (x, y, z, w).
fn rotate(rotation: [f32; 4], vector: [f32; 3]) -> [f32; 3]
{
	let conjugate = [-rotation[0], -rotation[1], -rotation[2], rotation[3]];

	rotate_by_inverse(conjugate, vector)
}

/// The velocity that carries a held point towards a target, limited in speed.
pub fn hold_velocity(target: [f32; 3], held: [f32; 3], stiffness: f32, max_speed: f32) -> [f32; 3]
{
	let velocity = [
		(target[0] - held[0]) * stiffness,
		(target[1] - held[1]) * stiffness,
		(target[2] - held[2]) * stiffness,
	];

	let speed = dot(velocity, velocity).sqrt();

	if speed > max_speed {
		velocity.map(|value| value * (max_speed / speed))
	} else {
		velocity
	}
}

impl<B: Backend> PhysicsWorld<B>
{
	pub fn new(backend: B) -> Self
	{
		Self {
			backend,
			paused: false,
			in_world: HashSet::new(),
		}
	}

	pub fn set_paused(&mut self, paused: bool)
	{
		self.paused = paused;
	}

	pub fn is_paused(&self) -> bool
	{
		self.paused
	}

	/// Advances the simulation by one fixed step, unless it is paused.
	pub fn update(&mut self)
	{
		if !self.paused {
			self.backend.step(TIME_STEP, 1);
		}
	}

	pub fn optimize(&mut self)
	{
		self.backend.optimize_broad_phase();
	}

	pub fn backend(&self) -> &B
	{
		&self.backend
	}

	pub fn backend_mut(&mut self) -> &mut B
	{
		&mut self.backend
	}

	/// Puts a body made from a shape in the world, replacing `previous` and keeping where it was.
	fn place(
		&mut self,
		shape: ShapeHandle,
		previous: Option<BodyId>,
		motion: Motion,
		props: &BodyProps,
	) -> Result<BodyId, PhysicsError>
	{
		let (position, rotation) = match previous {
			Some(previous) => {
				let state = self.backend.position_rotation(previous.0);

				self.destroy_body(previous);

				state
			}
			None => ([0.0; 3], [0.0, 0.0, 0.0, 1.0]),
		};

		let created = self.backend.create_body(&BodySpec {
			shape,
			position,
			rotation,
			dynamic: motion == Motion::Dynamic,
			layer: motion.layer(),
			friction: props.friction,
			restitution: props.restitution,
		});

		self.backend.release_shape(shape);

		let id = created.ok_or(PhysicsError::NoRoomForBody)?;

		self.backend.add_body(id, false);
		self.in_world.insert(id);

		Ok(BodyId(id))
	}

	/// A box with these full dimensions, which are kept to at least a centimetre. Returns the body
	/// and the dimensions used.
	pub fn create_box_body(
		&mut self,
		previous: Option<BodyId>,
		dimensions: [f32; 3],
		motion: Motion,
		props: &BodyProps,
	) -> Result<(BodyId, [f32; 3]), PhysicsError>
	{
		let dimensions = dimensions.map(|value| value.max(MIN_DIMENSION));
		let half = dimensions.map(|value| value * 0.5);

		let smallest = half[0].min(half[1]).min(half[2]);

		let shape = self
			.backend
			.make_box(half, props.density, props.convex_radius.min(smallest))
			.map_err(PhysicsError::Shape)?;

		Ok((self.place(shape, previous, motion, props)?, dimensions))
	}

	/// The convex hull of points relative to the body. Returns the body and the size of the points'
	/// bounds.
	pub fn create_hull_body(
		&mut self,
		previous: Option<BodyId>,
		points: &[[f32; 3]],
		motion: Motion,
		props: &BodyProps,
	) -> Result<(BodyId, [f32; 3]), PhysicsError>
	{
		if points.len() < MIN_HULL_POINTS {
			return Err(PhysicsError::TooFewPoints(points.len()));
		}

		let mut min = points[0];
		let mut max = points[0];

		for point in points {
			for axis in 0..3 {
				min[axis] = min[axis].min(point[axis]);
				max[axis] = max[axis].max(point[axis]);
			}
		}

		let shape = self
			.backend
			.make_hull(points, props.convex_radius, props.density)
			.map_err(PhysicsError::Shape)?;

		let body = self.place(shape, previous, motion, props)?;

		Ok((body, [max[0] - min[0], max[1] - min[1], max[2] - min[2]]))
	}

	/// A body shaped like a triangle mesh. A body that already exists is not replaced.
	pub fn create_mesh_body(
		&mut self,
		existing: Option<BodyId>,
		positions: &[[f32; 3]],
		indices: &[u32],
		motion: Motion,
		props: &BodyProps,
	) -> Result<BodyId, PhysicsError>
	{
		if existing.is_some() {
			return Err(PhysicsError::BodyExists);
		}

		let (triangles, rest) = indices.as_chunks::<3>();

		if !rest.is_empty() {
			return Err(PhysicsError::NotTriangles);
		}

		let shape = self
			.backend
			.make_mesh(positions, triangles)
			.map_err(PhysicsError::Shape)?;

		self.place(shape, None, motion, props)
	}

	pub fn remove_from_world(&mut self, body: BodyId)
	{
		if self.in_world.remove(&body.0) {
			self.backend.remove_body(body.0);
		}
	}

	pub fn add_to_world(&mut self, body: BodyId)
	{
		if self.in_world.insert(body.0) {
			self.backend.add_body(body.0, false);
		}
	}

	pub fn is_in_world(&self, body: BodyId) -> bool
	{
		self.in_world.contains(&body.0)
	}

	pub fn destroy_body(&mut self, body: BodyId)
	{
		self.remove_from_world(body);
		self.backend.destroy_body(body.0);
	}

	/// Moves a body and wakes it.
	pub fn teleport(&mut self, body: BodyId, position: [f32; 3], rotation: [f32; 4])
	{
		self.backend
			.set_position_rotation(body.0, position, rotation, true);
	}

	pub fn position_rotation(&self, body: BodyId) -> ([f32; 3], [f32; 4])
	{
		self.backend.position_rotation(body.0)
	}

	/// The first body a ray of `direction`'s length meets.
	pub fn raycast(
		&self,
		origin: [f32; 3],
		direction: [f32; 3],
		ignore: Option<BodyId>,
	) -> Option<RayResult>
	{
		let hit = self
			.backend
			.cast_ray(origin, direction, ignore.map(|body| body.0))?;

		let normal = if dot(hit.normal, direction) > 0.0 {
			hit.normal.map(|value| -value)
		} else {
			hit.normal
		};

		Some(RayResult {
			body: BodyId(hit.body),
			point: hit.point,
			normal,
		})
	}

	/// Every body the ray meets, the nearest first.
	pub fn raycast_objects(&self, origin: [f32; 3], direction: [f32; 3]) -> Vec<BodyId>
	{
		let mut hits = self.backend.cast_ray_all(origin, direction);

		hits.sort_by(|a, b| a.1.total_cmp(&b.1));

		hits.into_iter().map(|(body, _)| BodyId(body)).collect()
	}

	pub fn body_bounds(&self, body: BodyId) -> ([f32; 3], [f32; 3])
	{
		self.backend.body_bounds(body.0)
	}

	pub fn is_dynamic(&self, body: BodyId) -> bool
	{
		self.backend.is_dynamic(body.0)
	}

	pub fn is_active(&self, body: BodyId) -> bool
	{
		self.backend.is_active(body.0)
	}

	pub fn activate(&mut self, body: BodyId)
	{
		self.backend.activate(body.0);
	}

	pub fn deactivate(&mut self, body: BodyId)
	{
		self.backend.deactivate(body.0);
	}

	/// Pushes a body, waking it first.
	pub fn push(&mut self, body: BodyId, impulse: [f32; 3])
	{
		self.backend.activate(body.0);
		self.backend.add_impulse(body.0, impulse);
	}

	/// A world space point in the space of the body's centre of mass.
	pub fn point_to_local(&self, body: BodyId, point: [f32; 3]) -> [f32; 3]
	{
		let (center, rotation) = self.backend.center_of_mass(body.0);

		rotate_by_inverse(
			rotation,
			[
				point[0] - center[0],
				point[1] - center[1],
				point[2] - center[2],
			],
		)
	}

	/// A point in the space of the body's centre of mass as a world space point.
	pub fn point_to_world(&self, body: BodyId, local: [f32; 3]) -> [f32; 3]
	{
		let (center, rotation) = self.backend.center_of_mass(body.0);
		let rotated = rotate(rotation, local);

		[
			rotated[0] + center[0],
			rotated[1] + center[1],
			rotated[2] + center[2],
		]
	}

	/// Drags a point of a body towards a target by setting its velocity, and slows its spin. The
	/// body has to be dynamic and in the world.
	pub fn hold(
		&mut self,
		body: BodyId,
		local_point: [f32; 3],
		target: [f32; 3],
		stiffness: f32,
		max_speed: f32,
		angular_damping: f32,
	) -> Option<[f32; 3]>
	{
		if !self.backend.is_added(body.0) || !self.backend.is_dynamic(body.0) {
			return None;
		}

		let held = self.point_to_world(body, local_point);
		let velocity = hold_velocity(target, held, stiffness, max_speed);

		let spin = self
			.backend
			.angular_velocity(body.0)
			.map(|value| value * angular_damping);

		self.backend.activate(body.0);
		self.backend.set_velocities(body.0, velocity, spin);

		Some(held)
	}

	/// Which face of a box in its own space the ray from outside meets first, as an axis aligned
	/// unit vector, or zero if it is not clearly one of them.
	pub fn raycast_face_of_box(
		&self,
		body: BodyId,
		origin: [f32; 3],
		direction: [f32; 3],
	) -> [f32; 3]
	{
		let Some(world_normal) = self
			.backend
			.surface_normal_along_ray(body.0, origin, direction)
		else {
			return [0.0; 3];
		};

		let (_, rotation) = self.backend.position_rotation(body.0);
		let local = rotate_by_inverse(rotation, world_normal);

		let sign = |value: f32| if value < 0.0 { -1.0 } else { 1.0 };

		if local[0].abs() > 0.9 {
			[sign(local[0]), 0.0, 0.0]
		} else if local[1].abs() > 0.9 {
			[0.0, sign(local[1]), 0.0]
		} else if local[2].abs() > 0.9 {
			[0.0, 0.0, sign(local[2])]
		} else {
			[0.0; 3]
		}
	}
}

#[cfg(test)]
mod tests
{
	use super::*;
	use crate::backend::RayHit;

	#[derive(Default)]
	struct Fake
	{
		next_shape: u64,
		next_body: u32,
		boxes: Vec<([f32; 3], f32)>,
		bodies: Vec<(u32, [f32; 3], [f32; 4], Layer)>,
		destroyed: Vec<u32>,
		added: Vec<u32>,
		removed: Vec<u32>,
		hit: Option<RayHit>,
		all: Vec<(u32, f32)>,
		face: Option<[f32; 3]>,
		full: bool,
		woken: Vec<u32>,
		steps: Vec<(f32, u32)>,
		optimized: u32,
		impulses: Vec<(u32, [f32; 3])>,
		velocities: Vec<(u32, [f32; 3], [f32; 3])>,
	}

	impl Backend for Fake
	{
		fn make_box(
			&mut self,
			half: [f32; 3],
			_density: f32,
			radius: f32,
		) -> Result<ShapeHandle, String>
		{
			self.boxes.push((half, radius));
			self.next_shape += 1;

			Ok(ShapeHandle(self.next_shape))
		}

		fn make_hull(
			&mut self,
			points: &[[f32; 3]],
			_r: f32,
			_d: f32,
		) -> Result<ShapeHandle, String>
		{
			if points.iter().all(|p| *p == points[0]) {
				return Err("degenerate".to_owned());
			}

			Ok(ShapeHandle(1))
		}

		fn make_mesh(&mut self, _p: &[[f32; 3]], _t: &[[u32; 3]]) -> Result<ShapeHandle, String>
		{
			Ok(ShapeHandle(2))
		}

		fn release_shape(&mut self, _shape: ShapeHandle) {}

		fn create_body(&mut self, spec: &BodySpec) -> Option<u32>
		{
			if self.full {
				return None;
			}

			self.next_body += 1;
			self.bodies
				.push((self.next_body, spec.position, spec.rotation, spec.layer));

			Some(self.next_body)
		}

		fn add_body(&mut self, body: u32, _activate: bool)
		{
			self.added.push(body);
		}

		fn remove_body(&mut self, body: u32)
		{
			self.removed.push(body);
		}

		fn destroy_body(&mut self, body: u32)
		{
			self.destroyed.push(body);
		}

		fn set_position_rotation(
			&mut self,
			body: u32,
			position: [f32; 3],
			rotation: [f32; 4],
			_a: bool,
		)
		{
			for entry in &mut self.bodies {
				if entry.0 == body {
					entry.1 = position;
					entry.2 = rotation;
				}
			}
		}

		fn position_rotation(&self, body: u32) -> ([f32; 3], [f32; 4])
		{
			self.bodies
				.iter()
				.find(|entry| entry.0 == body)
				.map_or(([0.0; 3], [0.0, 0.0, 0.0, 1.0]), |entry| (entry.1, entry.2))
		}

		fn cast_ray(&self, _o: [f32; 3], _d: [f32; 3], _ignore: Option<u32>) -> Option<RayHit>
		{
			self.hit
		}

		fn cast_ray_all(&self, _o: [f32; 3], _d: [f32; 3]) -> Vec<(u32, f32)>
		{
			self.all.clone()
		}

		fn surface_normal_along_ray(&self, _b: u32, _o: [f32; 3], _d: [f32; 3])
		-> Option<[f32; 3]>
		{
			self.face
		}

		fn step(&mut self, delta_time: f32, collision_steps: u32)
		{
			self.steps.push((delta_time, collision_steps));
		}

		fn optimize_broad_phase(&mut self)
		{
			self.optimized += 1;
		}

		fn is_dynamic(&self, body: u32) -> bool
		{
			self.bodies
				.iter()
				.any(|entry| entry.0 == body && entry.3 == Layer::Dynamic)
		}

		fn body_bounds(&self, body: u32) -> ([f32; 3], [f32; 3])
		{
			let (position, _) = self.position_rotation(body);

			(
				position.map(|value| value - 1.0),
				position.map(|value| value + 1.0),
			)
		}

		fn is_added(&self, body: u32) -> bool
		{
			self.added.contains(&body) && !self.removed.contains(&body)
		}

		fn is_active(&self, body: u32) -> bool
		{
			self.woken.contains(&body)
		}

		fn activate(&mut self, body: u32)
		{
			self.woken.push(body);
		}

		fn deactivate(&mut self, body: u32)
		{
			self.woken.retain(|woken| *woken != body);
		}

		fn add_impulse(&mut self, body: u32, impulse: [f32; 3])
		{
			self.impulses.push((body, impulse));
		}

		fn center_of_mass(&self, body: u32) -> ([f32; 3], [f32; 4])
		{
			self.position_rotation(body)
		}

		fn angular_velocity(&self, _body: u32) -> [f32; 3]
		{
			[2.0, 0.0, 0.0]
		}

		fn set_velocities(&mut self, body: u32, linear: [f32; 3], angular: [f32; 3])
		{
			self.velocities.push((body, linear, angular));
		}
	}

	fn world() -> PhysicsWorld<Fake>
	{
		PhysicsWorld::new(Fake::default())
	}

	#[test]
	fn a_box_has_at_least_a_centimetre_each_way_and_a_convex_radius_that_fits()
	{
		let mut world = world();

		let (body, dimensions) = world
			.create_box_body(
				None,
				[0.0, 2.0, -1.0],
				Motion::Static,
				&BodyProps::default(),
			)
			.unwrap();

		assert_eq!(dimensions, [0.01, 2.0, 0.01]);
		assert_eq!(
			world.backend().boxes[0],
			([0.005, 1.0, 0.005], 0.001_f32.min(0.005))
		);
		assert!(world.is_in_world(body));
		assert_eq!(world.backend().bodies[0].3, Layer::Static);
	}

	#[test]
	fn replacing_a_body_keeps_where_it_was()
	{
		let mut world = world();
		let props = BodyProps::default();

		let (first, _) = world
			.create_box_body(None, [1.0; 3], Motion::Dynamic, &props)
			.unwrap();

		world.teleport(first, [4.0, 5.0, 6.0], [0.0, 1.0, 0.0, 0.0]);

		let (second, _) = world
			.create_box_body(Some(first), [2.0; 3], Motion::Dynamic, &props)
			.unwrap();

		assert_ne!(first, second);
		assert!(!world.is_in_world(first));
		assert_eq!(world.backend().destroyed, vec![first.0]);
		assert_eq!(
			world.position_rotation(second),
			([4.0, 5.0, 6.0], [0.0, 1.0, 0.0, 0.0])
		);
	}

	#[test]
	fn a_hull_needs_four_points_and_reports_the_size_of_its_bounds()
	{
		let mut world = world();
		let props = BodyProps::default();

		assert_eq!(
			world
				.create_hull_body(None, &[[0.0; 3]; 3], Motion::Static, &props)
				.unwrap_err(),
			PhysicsError::TooFewPoints(3)
		);

		let points = [
			[0.0, 0.0, 0.0],
			[2.0, 0.0, 0.0],
			[0.0, 3.0, 0.0],
			[0.0, 0.0, 4.0],
		];
		let (_, size) = world
			.create_hull_body(None, &points, Motion::Static, &props)
			.unwrap();

		assert_eq!(size, [2.0, 3.0, 4.0]);
	}

	#[test]
	fn a_mesh_must_be_whole_triangles_and_does_not_replace_a_body()
	{
		let mut world = world();
		let props = BodyProps::default();
		let positions = [[0.0; 3], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]];

		assert_eq!(
			world
				.create_mesh_body(None, &positions, &[0, 1], Motion::Static, &props)
				.unwrap_err(),
			PhysicsError::NotTriangles
		);
		assert_eq!(
			world
				.create_mesh_body(
					Some(BodyId(1)),
					&positions,
					&[0, 1, 2],
					Motion::Static,
					&props
				)
				.unwrap_err(),
			PhysicsError::BodyExists
		);
		assert!(
			world
				.create_mesh_body(None, &positions, &[0, 1, 2], Motion::Static, &props)
				.is_ok()
		);
	}

	#[test]
	fn a_full_world_reports_no_room()
	{
		let mut world = world();
		world.backend_mut().full = true;

		assert_eq!(
			world
				.create_box_body(None, [1.0; 3], Motion::Static, &BodyProps::default())
				.unwrap_err(),
			PhysicsError::NoRoomForBody
		);
	}

	#[test]
	fn a_body_is_added_and_removed_once()
	{
		let mut world = world();

		let (body, _) = world
			.create_box_body(None, [1.0; 3], Motion::Static, &BodyProps::default())
			.unwrap();

		world.add_to_world(body);
		world.remove_from_world(body);
		world.remove_from_world(body);

		assert_eq!(world.backend().added, vec![body.0]);
		assert_eq!(world.backend().removed, vec![body.0]);
	}

	#[test]
	fn a_ray_hit_has_its_normal_turned_towards_the_ray()
	{
		let mut world = world();

		world.backend_mut().hit = Some(RayHit {
			body: 3,
			point: [1.0, 2.0, 3.0],
			normal: [0.0, 0.0, 1.0],
		});

		let hit = world.raycast([0.0; 3], [0.0, 0.0, 5.0], None).unwrap();

		assert_eq!(hit.normal, [0.0, 0.0, -1.0]);
		assert_eq!(hit.body, BodyId(3));

		let facing = world.raycast([0.0; 3], [0.0, 0.0, -5.0], None).unwrap();

		assert_eq!(facing.normal, [0.0, 0.0, 1.0]);
	}

	#[test]
	fn bodies_the_ray_passes_through_come_back_nearest_first()
	{
		let mut world = world();

		world.backend_mut().all = vec![(7, 0.9), (2, 0.1), (5, 0.5)];

		assert_eq!(
			world.raycast_objects([0.0; 3], [1.0, 0.0, 0.0]),
			vec![BodyId(2), BodyId(5), BodyId(7)]
		);
	}

	#[test]
	fn the_face_of_a_box_is_found_in_the_boxs_own_space()
	{
		let mut world = world();
		let (body, _) = world
			.create_box_body(None, [1.0; 3], Motion::Static, &BodyProps::default())
			.unwrap();

		world.backend_mut().face = Some([0.0, 0.0, 1.0]);

		assert_eq!(
			world.raycast_face_of_box(body, [0.0; 3], [0.0, 0.0, 1.0]),
			[0.0, 0.0, 1.0]
		);

		let quarter_turn = std::f32::consts::FRAC_1_SQRT_2;

		world.teleport(body, [0.0; 3], [0.0, quarter_turn, 0.0, quarter_turn]);
		world.backend_mut().face = Some([1.0, 0.0, 0.0]);

		let face = world.raycast_face_of_box(body, [0.0; 3], [1.0, 0.0, 0.0]);

		assert!(face[2].abs() > 0.99 && face[0].abs() < 1e-3, "{face:?}");

		world.backend_mut().face = Some([0.6, 0.6, 0.5]);

		assert_eq!(
			world.raycast_face_of_box(body, [0.0; 3], [1.0, 0.0, 0.0]),
			[0.0; 3]
		);
	}

	#[test]
	fn pushing_wakes_the_body_then_applies_the_impulse()
	{
		let mut world = world();
		let (body, _) = world
			.create_box_body(None, [1.0; 3], Motion::Dynamic, &BodyProps::default())
			.unwrap();

		assert!(world.is_dynamic(body));
		assert!(!world.is_active(body));

		world.push(body, [0.0, 3.0, 0.0]);

		assert!(world.is_active(body));
		assert_eq!(world.backend().impulses, vec![(body.0, [0.0, 3.0, 0.0])]);

		world.deactivate(body);

		assert!(!world.is_active(body));
	}

	#[test]
	fn a_point_round_trips_through_the_bodys_local_space()
	{
		let mut world = world();
		let (body, _) = world
			.create_box_body(None, [1.0; 3], Motion::Dynamic, &BodyProps::default())
			.unwrap();

		let quarter_turn = std::f32::consts::FRAC_1_SQRT_2;

		world.teleport(
			body,
			[1.0, 2.0, 3.0],
			[0.0, quarter_turn, 0.0, quarter_turn],
		);

		let point = [4.0, 5.0, 6.0];
		let back = world.point_to_world(body, world.point_to_local(body, point));

		for axis in 0..3 {
			assert!((back[axis] - point[axis]).abs() < 1e-4, "{back:?}");
		}
	}

	#[test]
	fn holding_a_body_sets_a_speed_limited_velocity_and_damps_its_spin()
	{
		let mut world = world();
		let (body, _) = world
			.create_box_body(None, [1.0; 3], Motion::Dynamic, &BodyProps::default())
			.unwrap();

		let held = world
			.hold(body, [0.0; 3], [100.0, 0.0, 0.0], 10.0, 5.0, 0.5)
			.unwrap();

		assert_eq!(held, [0.0; 3]);

		let (_, linear, angular) = world.backend().velocities[0];

		assert!((linear[0] - 5.0).abs() < 1e-4);
		assert_eq!(angular, [1.0, 0.0, 0.0]);

		let (_, stat) = world
			.create_box_body(None, [1.0; 3], Motion::Static, &BodyProps::default())
			.unwrap();

		let _ = stat;
		world.remove_from_world(body);

		assert!(
			world
				.hold(body, [0.0; 3], [1.0; 3], 1.0, 1.0, 1.0)
				.is_none()
		);
	}

	#[test]
	fn a_slow_pull_is_not_limited()
	{
		assert_eq!(
			hold_velocity([1.0, 0.0, 0.0], [0.0; 3], 2.0, 10.0),
			[2.0, 0.0, 0.0]
		);
	}

	#[test]
	fn updating_steps_the_simulation_by_a_sixtieth_unless_paused()
	{
		let mut world = world();

		world.update();
		world.set_paused(true);
		world.update();
		world.set_paused(false);
		world.update();
		world.optimize();

		assert_eq!(world.backend().steps, vec![(TIME_STEP, 1), (TIME_STEP, 1)]);
		assert_eq!(world.backend().optimized, 1);
		assert!(!world.is_paused());
	}
}
