use raptor_physics::{Backend, BodyId, BodyProps, Motion, PhysicsError, PhysicsWorld, RayResult};

pub const MAX_COLLIDERS: usize = 256;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct ColliderId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Primitive {
	None,
	Box,
}

#[derive(Clone, Debug)]
pub struct Collider {
	pub id: ColliderId,
	pub name: String,
	pub name_hash: u32,
	pub body: Option<BodyId>,
	pub motion: Motion,
	pub primitive: Primitive,
	pub dimensions: [f32; 3],
	pub midpoint: [f32; 3],
	pub object: Option<u32>,
}

pub fn hash_name(name: &str) -> u32 {
	let mut hash: u32 = 0x811c_9dc5;

	for byte in name.bytes() {
		hash ^= u32::from(byte);
		hash = hash.wrapping_mul(0x0100_0193);
	}

	hash
}

pub struct Colliders {
	slots: Vec<Option<Collider>>,
	update_state: u32,
}

impl Default for Colliders {
	fn default() -> Self {
		Self::new()
	}
}

impl Colliders {
	pub fn new() -> Self {
		Self {
			slots: (0..MAX_COLLIDERS).map(|_| None).collect(),
			update_state: 0,
		}
	}

	pub fn update_state(&self) -> u32 {
		self.update_state
	}

	pub fn create(&mut self, name: &str) -> Option<ColliderId> {
		let index = self.slots.iter().position(Option::is_none)?;
		let id = ColliderId(index as u32);

		self.slots[index] = Some(Collider {
			id,
			name: name.to_owned(),
			name_hash: hash_name(name),
			body: None,
			motion: Motion::Static,
			primitive: Primitive::None,
			dimensions: [1.0; 3],
			midpoint: [0.0; 3],
			object: None,
		});

		self.update_state = self.update_state.wrapping_add(1);

		Some(id)
	}

	pub fn get(&self, id: ColliderId) -> Option<&Collider> {
		self.slots.get(id.0 as usize)?.as_ref()
	}

	pub fn get_mut(&mut self, id: ColliderId) -> Option<&mut Collider> {
		self.slots.get_mut(id.0 as usize)?.as_mut()
	}

	pub fn iter(&self) -> impl Iterator<Item = &Collider> {
		self.slots.iter().flatten()
	}

	pub fn find_by_name_hash(&self, hash: u32) -> Option<ColliderId> {
		self.iter()
			.find(|collider| collider.name_hash == hash)
			.map(|collider| collider.id)
	}

	pub fn find_by_body(&self, body: BodyId) -> Option<ColliderId> {
		self.iter()
			.find(|collider| collider.body == Some(body))
			.map(|collider| collider.id)
	}

	pub fn destroy<B: Backend>(&mut self, id: ColliderId, world: &mut PhysicsWorld<B>) {
		let Some(slot) = self.slots.get_mut(id.0 as usize) else {
			return;
		};

		if let Some(collider) = slot.take()
			&& let Some(body) = collider.body
		{
			world.destroy_body(body);
		}

		self.update_state = self.update_state.wrapping_add(1);
	}

	pub fn create_box<B: Backend>(
		&mut self,
		id: ColliderId,
		world: &mut PhysicsWorld<B>,
		dimensions: [f32; 3],
		motion: Motion,
		props: &BodyProps,
	) -> Result<(), PhysicsError> {
		let collider = self.get_mut(id).ok_or(PhysicsError::NoRoomForBody)?;

		collider.motion = motion;
		collider.primitive = Primitive::Box;

		let (body, used) = world.create_box_body(collider.body, dimensions, motion, props)?;

		collider.body = Some(body);
		collider.dimensions = used;

		Ok(())
	}

	pub fn create_hull<B: Backend>(
		&mut self,
		id: ColliderId,
		world: &mut PhysicsWorld<B>,
		points: &[[f32; 3]],
		motion: Motion,
		props: &BodyProps,
	) -> Result<(), PhysicsError> {
		let collider = self.get_mut(id).ok_or(PhysicsError::NoRoomForBody)?;

		collider.motion = motion;
		collider.primitive = Primitive::None;

		let (body, size) = world.create_hull_body(collider.body, points, motion, props)?;

		collider.body = Some(body);
		collider.dimensions = size;

		Ok(())
	}

	pub fn create_mesh<B: Backend>(
		&mut self,
		id: ColliderId,
		world: &mut PhysicsWorld<B>,
		positions: &[[f32; 3]],
		indices: &[u32],
		motion: Motion,
		props: &BodyProps,
	) -> Result<(), PhysicsError> {
		let collider = self.get_mut(id).ok_or(PhysicsError::NoRoomForBody)?;

		collider.motion = motion;

		let body = world.create_mesh_body(collider.body, positions, indices, motion, props)?;

		collider.body = Some(body);

		Ok(())
	}

	pub fn destroy_body<B: Backend>(&mut self, id: ColliderId, world: &mut PhysicsWorld<B>) {
		if let Some(collider) = self.get_mut(id)
			&& let Some(body) = collider.body.take()
		{
			world.destroy_body(body);
		}
	}

	pub fn teleport<B: Backend>(
		&self,
		id: ColliderId,
		world: &mut PhysicsWorld<B>,
		position: [f32; 3],
		rotation: [f32; 4],
	) {
		let Some(collider) = self.get(id) else {
			return;
		};

		let Some(body) = collider.body else {
			return;
		};

		let target = [
			position[0] + collider.midpoint[0],
			position[1] + collider.midpoint[1],
			position[2] + collider.midpoint[2],
		];

		world.teleport(body, target, rotation);
	}

	pub fn position_rotation<B: Backend>(
		&self,
		id: ColliderId,
		world: &PhysicsWorld<B>,
	) -> Option<([f32; 3], [f32; 4])> {
		let body = self.get(id)?.body?;

		Some(world.position_rotation(body))
	}

	pub fn world_bounds<B: Backend>(
		&self,
		id: ColliderId,
		world: &PhysicsWorld<B>,
	) -> ([f32; 3], [f32; 3]) {
		match self.get(id).and_then(|collider| collider.body) {
			Some(body) => world.body_bounds(body),
			None => ([0.0; 3], [0.0; 3]),
		}
	}

	pub fn remove_from_world<B: Backend>(&self, id: ColliderId, world: &mut PhysicsWorld<B>) {
		if let Some(body) = self.get(id).and_then(|collider| collider.body) {
			world.remove_from_world(body);
		}
	}

	pub fn add_to_world<B: Backend>(&self, id: ColliderId, world: &mut PhysicsWorld<B>) {
		if let Some(body) = self.get(id).and_then(|collider| collider.body) {
			world.add_to_world(body);
		}
	}

	pub fn raycast_collider<B: Backend>(
		&self,
		world: &PhysicsWorld<B>,
		origin: [f32; 3],
		direction: [f32; 3],
		ignore: Option<BodyId>,
	) -> Option<(RayResult, Option<ColliderId>)> {
		let hit = world.raycast(origin, direction, ignore)?;
		let collider = self.find_by_body(hit.body);

		Some((hit, collider))
	}
}

#[cfg(test)]
mod tests {
	use raptor_jolt::JoltBackend;

	use super::*;

	fn world() -> PhysicsWorld<JoltBackend> {
		PhysicsWorld::new(JoltBackend::new().unwrap())
	}

	#[test]
	fn a_box_collider_is_found_by_its_name_and_its_body() {
		let mut world = world();
		let mut colliders = Colliders::new();

		let id = colliders.create("crate").unwrap();

		colliders
			.create_box(
				id,
				&mut world,
				[1.0; 3],
				Motion::Dynamic,
				&BodyProps::default(),
			)
			.unwrap();

		let body = colliders.get(id).unwrap().body.unwrap();

		assert_eq!(colliders.find_by_name_hash(hash_name("crate")), Some(id));
		assert_eq!(colliders.find_by_body(body), Some(id));
		assert_eq!(colliders.get(id).unwrap().dimensions, [1.0; 3]);
	}

	#[test]
	fn destroying_a_collider_removes_its_body() {
		let mut world = world();
		let mut colliders = Colliders::new();

		let id = colliders.create("crate").unwrap();

		colliders
			.create_box(
				id,
				&mut world,
				[1.0; 3],
				Motion::Static,
				&BodyProps::default(),
			)
			.unwrap();

		let body = colliders.get(id).unwrap().body.unwrap();
		let state = colliders.update_state();

		colliders.destroy(id, &mut world);

		assert!(colliders.get(id).is_none());
		assert!(!world.is_in_world(body));
		assert_ne!(colliders.update_state(), state);
	}

	#[test]
	fn teleporting_adds_the_midpoint() {
		let mut world = world();
		let mut colliders = Colliders::new();

		let id = colliders.create("crate").unwrap();

		colliders
			.create_box(
				id,
				&mut world,
				[1.0; 3],
				Motion::Static,
				&BodyProps::default(),
			)
			.unwrap();

		colliders.get_mut(id).unwrap().midpoint = [0.0, 0.5, 0.0];
		colliders.teleport(id, &mut world, [1.0, 2.0, 3.0], [0.0, 0.0, 0.0, 1.0]);

		let (position, _) = colliders.position_rotation(id, &world).unwrap();

		assert_eq!(position, [1.0, 2.5, 3.0]);
	}

	#[test]
	fn the_slots_run_out_at_the_capacity() {
		let mut colliders = Colliders::new();

		for index in 0..MAX_COLLIDERS {
			assert!(colliders.create(&format!("c{index}")).is_some());
		}

		assert!(colliders.create("one too many").is_none());
	}
}
