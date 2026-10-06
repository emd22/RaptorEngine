use raptor_entity::object_core::{
	FLAG_DISABLE_CULLING, FLAG_HAS_MESH, FLAG_IS_INSTANCE, FLAG_NOT_PROBE_VISIBLE,
	FLAG_PHYSICS_ENABLED, FLAG_SHADOW_CASTER, FLAG_SKINNED, LAYER_WORLD, NO_BODY, NO_ID,
	TAG_PROBE_VOLUME,
};
use raptor_entity::{EntityCore, ObjectCore};
use raptor_jolt::JoltBackend;
use raptor_math::{Aabb, Mat4f, Obb, Vec3f};
use raptor_physics::{Backend, PhysicsWorld};
use raptor_world::grid::{Aabb as GridAabb, ObjectUpdate};
use raptor_world::object_logic::{
	CullInputs, can_be_frustum_culled, contains_point, direction_scale, merge_child_bounds,
	raycast_bounds,
};
use raptor_world::object_store::ObjectStore;
use raptor_world::{NULL_TILE, WorldGrid};

use crate::colliders::{ColliderId, Colliders, hash_name};

pub const MAX_OBJECTS: u32 = 1024;

pub const FLAG_READY_TO_RENDER: u16 = 1 << 0;
pub const FLAG_UNLIT: u16 = 1 << 4;
pub const FLAG_SHARED_MESH: u16 = 1 << 7;

pub const TAG_REFLECTION_PROBE: u32 = 1 << 3;

pub type ObjectId = u32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct MeshRef {
	pub id: u32,
	pub skinned: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SkeletonRef(pub u32);

#[derive(Clone, Debug, Default)]
pub struct Node {
	pub name: String,
	pub mesh: Option<MeshRef>,
	pub skeleton: Option<SkeletonRef>,
	pub added_to_world: bool,
}

pub struct Scene<B: Backend = JoltBackend> {
	objects: ObjectStore,
	nodes: Vec<Option<Node>>,
	pub grid: WorldGrid,
	pub physics: PhysicsWorld<B>,
	pub colliders: Colliders,
}

fn grid_bounds(bounds: Aabb) -> GridAabb {
	GridAabb {
		min: [bounds.min.x, bounds.min.y, bounds.min.z],
		max: [bounds.max.x, bounds.max.y, bounds.max.z],
	}
}

fn aabb_of(core: &ObjectCore) -> Aabb {
	Aabb::new(
		Vec3f::new(core.bounds_min[0], core.bounds_min[1], core.bounds_min[2]),
		Vec3f::new(core.bounds_max[0], core.bounds_max[1], core.bounds_max[2]),
	)
}

fn store_bounds(core: &mut ObjectCore, bounds: Aabb) {
	core.bounds_min = [bounds.min.x, bounds.min.y, bounds.min.z, 0.0];
	core.bounds_max = [bounds.max.x, bounds.max.y, bounds.max.z, 0.0];
}

fn vec(values: [f32; 4]) -> Vec3f {
	Vec3f::new(values[0], values[1], values[2])
}

fn close(a: [f32; 3], b: [f32; 3]) -> bool {
	(0..3).all(|axis| (a[axis] - b[axis]).abs() < 1.0e-5)
}

fn quat_close(a: [f32; 4], b: [f32; 4]) -> bool {
	let same = (0..4).all(|axis| (a[axis] - b[axis]).abs() < 1.0e-5);
	let flipped = (0..4).all(|axis| (a[axis] + b[axis]).abs() < 1.0e-5);

	same || flipped
}

fn quat_mul(a: [f32; 4], b: [f32; 4]) -> [f32; 4] {
	[
		a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
		a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
		a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
		a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
	]
}

fn quat_conjugate(a: [f32; 4]) -> [f32; 4] {
	[-a[0], -a[1], -a[2], a[3]]
}

impl<B: Backend> Scene<B> {
	pub fn new(backend: B) -> Self {
		Self {
			objects: ObjectStore::new(MAX_OBJECTS),
			nodes: (0..MAX_OBJECTS).map(|_| None).collect(),
			grid: WorldGrid::default(),
			physics: PhysicsWorld::new(backend),
			colliders: Colliders::new(),
		}
	}

	pub fn objects(&self) -> &ObjectStore {
		&self.objects
	}

	pub fn is_used(&self, id: ObjectId) -> bool {
		self.objects.is_used(id)
	}

	pub fn used_ids(&self) -> Vec<ObjectId> {
		self.objects.used_ids()
	}

	pub fn ids_with_tags(&self, tags: u32) -> Vec<ObjectId> {
		self.objects.ids_with_tags(tags)
	}

	pub fn find_by_name(&self, name: &str) -> Option<ObjectId> {
		self.objects.find_by_name_hash(hash_name(name))
	}

	pub fn node(&self, id: ObjectId) -> Option<&Node> {
		self.nodes.get(id as usize)?.as_ref()
	}

	pub fn node_mut(&mut self, id: ObjectId) -> Option<&mut Node> {
		self.nodes.get_mut(id as usize)?.as_mut()
	}

	pub fn entity(&self, id: ObjectId) -> Option<&EntityCore> {
		let handles = self.objects.get(id)?;

		// SAFETY: the record is boxed, so the pointer stays good until the object is freed, and
		// the scene is borrowed for as long as the reference lives.
		Some(unsafe { &*handles.entity })
	}

	pub fn entity_mut(&mut self, id: ObjectId) -> Option<&mut EntityCore> {
		let handles = self.objects.get(id)?;

		// SAFETY: as above, and the scene is borrowed mutably, so no other reference to the record
		// is made through it.
		Some(unsafe { &mut *handles.entity })
	}

	pub fn core(&self, id: ObjectId) -> Option<&ObjectCore> {
		let handles = self.objects.get(id)?;

		// SAFETY: as for `entity`.
		Some(unsafe { &*handles.object })
	}

	pub fn core_mut(&mut self, id: ObjectId) -> Option<&mut ObjectCore> {
		let handles = self.objects.get(id)?;

		// SAFETY: as for `entity_mut`.
		Some(unsafe { &mut *handles.object })
	}

	pub fn new_object(&mut self, name: &str, material: u32, tags: u32) -> Option<ObjectId> {
		let handles = self.objects.alloc(hash_name(name))?;

		let id = handles.id;

		self.nodes[id as usize] = Some(Node {
			name: name.to_owned(),
			..Node::default()
		});

		let core = self.core_mut(id)?;

		core.material_id = material;
		core.tags = tags;

		Some(id)
	}

	pub fn name(&self, id: ObjectId) -> &str {
		self.node(id).map_or("", |node| node.name.as_str())
	}

	pub fn set_name(&mut self, id: ObjectId, name: &str) {
		if let Some(node) = self.node_mut(id) {
			node.name = name.to_owned();
		}

		self.objects.set_name_hash(id, hash_name(name));
	}

	pub fn world_matrix(&mut self, id: ObjectId) -> Option<Mat4f> {
		Some(Mat4f::from_rows(self.entity_mut(id)?.world_matrix()))
	}

	pub fn position(&self, id: ObjectId) -> Option<[f32; 3]> {
		let entity = self.entity(id)?;

		Some([entity.position[0], entity.position[1], entity.position[2]])
	}

	pub fn rotation(&self, id: ObjectId) -> Option<[f32; 4]> {
		Some(self.entity(id)?.rotation)
	}

	pub fn scale(&self, id: ObjectId) -> Option<f32> {
		Some(self.entity(id)?.scale)
	}

	pub fn bounds(&self, id: ObjectId) -> Option<Aabb> {
		Some(aabb_of(self.core(id)?))
	}

	pub fn world_obb(&mut self, id: ObjectId) -> Option<Obb> {
		let bounds = self.bounds(id)?;
		let matrix = self.world_matrix(id)?;

		Some(Obb::from_local_bounds(&bounds, &matrix))
	}

	pub fn world_aabb(&mut self, id: ObjectId) -> Option<Aabb> {
		Some(self.world_obb(id)?.world_aabb())
	}

	pub fn is_cullable(&self, id: ObjectId) -> bool {
		self.core(id).is_some_and(ObjectCore::is_cullable)
	}

	fn grid_bounds_of(&mut self, id: ObjectId) -> Option<GridAabb> {
		Some(grid_bounds(self.world_aabb(id)?))
	}

	pub fn add_to_grid(&mut self, id: ObjectId) {
		let cullable = self.is_cullable(id);

		if let Some(bounds) = self.grid_bounds_of(id) {
			self.grid.add_object(id, bounds, cullable);
		}
	}

	pub fn remove_from_grid(&mut self, id: ObjectId) {
		if self.is_used(id) {
			self.grid.remove_object(id);
		}
	}

	pub fn update_in_grid(&mut self, id: ObjectId, update_attached: bool) {
		let cullable = self.is_cullable(id);

		let Some(bounds) = self.grid_bounds_of(id) else {
			return;
		};

		let result = self.grid.update_object(id, bounds, cullable);

		if result != ObjectUpdate::Moved || !update_attached {
			return;
		}

		let tile = self.grid.object_tile(id);

		let children: Vec<u32> = self
			.core(id)
			.map(|core| core.children().to_vec())
			.unwrap_or_default();

		for child in children {
			if self.is_used(child) && self.grid.object_tile(child) != tile {
				self.update_in_grid(child, true);
			}
		}
	}

	pub fn set_material(&mut self, id: ObjectId, material: u32) {
		if let Some(core) = self.core_mut(id) {
			core.material_id = material;
		}
	}

	pub fn set_mesh(&mut self, id: ObjectId, mesh: Option<MeshRef>) {
		if let Some(node) = self.node_mut(id) {
			node.mesh = mesh;
		}

		if let Some(core) = self.core_mut(id) {
			core.set_flag(FLAG_HAS_MESH, mesh.is_some());
			core.set_flag(FLAG_SKINNED, mesh.is_some_and(|mesh| mesh.skinned));
		}
	}

	pub fn mesh(&self, id: ObjectId) -> Option<MeshRef> {
		self.node(id)?.mesh
	}

	pub fn is_skinned(&self, id: ObjectId) -> bool {
		self.mesh(id).is_some_and(|mesh| mesh.skinned)
	}

	fn children_of(&self, id: ObjectId) -> Vec<u32> {
		self.core(id)
			.map(|core| core.children().to_vec())
			.unwrap_or_default()
	}

	pub fn collider_of(&self, id: ObjectId) -> Option<ColliderId> {
		let physics_id = self.core(id)?.physics_id;

		(physics_id != NO_BODY).then_some(ColliderId(physics_id))
	}

	pub fn attach_collider(&mut self, id: ObjectId, collider: ColliderId) {
		if let Some(core) = self.core_mut(id) {
			core.physics_id = collider.0;
		}

		if let Some(collider) = self.colliders.get_mut(collider) {
			collider.object = Some(id);
		}
	}

	pub fn set_position(&mut self, id: ObjectId, position: [f32; 3]) {
		let Some(current) = self.position(id) else {
			return;
		};

		let delta = [
			position[0] - current[0],
			position[1] - current[1],
			position[2] - current[2],
		];

		if let Some(entity) = self.entity_mut(id) {
			entity.set_position(position);
		}

		self.update_in_grid(id, true);

		let rotation = self.rotation(id).unwrap_or([0.0, 0.0, 0.0, 1.0]);

		if let Some(collider) = self.collider_of(id) {
			self.colliders
				.teleport(collider, &mut self.physics, position, rotation);
		}

		for child in self.children_of(id) {
			self.move_by(child, delta);
		}
	}

	pub fn move_by(&mut self, id: ObjectId, offset: [f32; 3]) {
		if let Some(position) = self.position(id) {
			self.set_position(
				id,
				[
					position[0] + offset[0],
					position[1] + offset[1],
					position[2] + offset[2],
				],
			);
		}
	}

	pub fn set_rotation(&mut self, id: ObjectId, rotation: [f32; 4]) {
		let Some(current) = self.rotation(id) else {
			return;
		};

		let delta = quat_mul(rotation, quat_conjugate(current));

		if let Some(entity) = self.entity_mut(id) {
			entity.set_rotation(rotation);
		}

		self.update_in_grid(id, true);

		let position = self.position(id).unwrap_or([0.0; 3]);

		if let Some(collider) = self.collider_of(id) {
			self.colliders
				.teleport(collider, &mut self.physics, position, rotation);
		}

		for child in self.children_of(id) {
			if let Some(child_rotation) = self.rotation(child) {
				self.set_rotation(child, quat_mul(delta, child_rotation));
			}
		}
	}

	pub fn rotate_by_axis(&mut self, id: ObjectId, axis: [f32; 3], radians: f32) {
		let Some(rotation) = self
			.entity(id)
			.map(|entity| entity.rotated_by_axis(axis, radians))
		else {
			return;
		};

		self.set_rotation(id, rotation);
	}

	pub fn set_scale(&mut self, id: ObjectId, scale: f32) {
		if let Some(entity) = self.entity_mut(id) {
			entity.set_scale(scale);
		}

		self.update_in_grid(id, true);
	}

	pub fn scale_by(&mut self, id: ObjectId, factor: f32) {
		if let Some(scale) = self.scale(id) {
			self.set_scale(id, scale * factor);
		}
	}

	pub fn attach_object(&mut self, parent: ObjectId, child: ObjectId) {
		let (Some(position), Some(scale)) = (self.position(parent), self.scale(parent)) else {
			return;
		};

		if let Some(core) = self.core_mut(child) {
			core.parent_id = parent;
		}

		self.move_by(child, position);
		self.scale_by(child, scale);

		if let Some(core) = self.core_mut(parent) {
			core.add_child(child);
		}
	}

	pub fn set_object_layer(&mut self, id: ObjectId, layer: u32) {
		if let Some(core) = self.core_mut(id) {
			core.layer = layer;
		}

		for child in self.children_of(id) {
			self.set_object_layer(child, layer);
		}
	}

	pub fn set_shadow_caster(&mut self, id: ObjectId, value: bool) {
		if let Some(core) = self.core_mut(id) {
			core.set_flag(FLAG_SHADOW_CASTER, value);
		}
	}

	pub fn set_unlit(&mut self, id: ObjectId, value: bool) {
		if let Some(core) = self.core_mut(id) {
			core.set_flag(FLAG_UNLIT, value);
		}
	}

	pub fn set_cullable(&mut self, id: ObjectId, value: bool) {
		if let Some(core) = self.core_mut(id) {
			core.set_flag(FLAG_DISABLE_CULLING, !value);
		}

		self.update_in_grid(id, true);
	}

	pub fn set_probe_visible(&mut self, id: ObjectId, value: bool) {
		if let Some(core) = self.core_mut(id) {
			core.set_flag(FLAG_NOT_PROBE_VISIBLE, !value);
		}

		for child in self.children_of(id) {
			self.set_probe_visible(child, value);
		}
	}

	pub fn set_probe_volume(&mut self, id: ObjectId, value: bool) {
		if let Some(core) = self.core_mut(id) {
			core.set_tag(TAG_PROBE_VOLUME, value);

			if !value {
				core.set_tag(TAG_REFLECTION_PROBE, false);
			}
		}

		self.set_probe_visible(id, !value);
		self.set_shadow_caster(id, !value);

		if let Some(collider) = self.collider_of(id) {
			if value {
				self.colliders
					.remove_from_world(collider, &mut self.physics);
			} else {
				self.colliders.add_to_world(collider, &mut self.physics);
			}
		}
	}

	pub fn set_reflection_probe(&mut self, id: ObjectId, value: bool) {
		self.set_probe_volume(id, value);

		if value && let Some(core) = self.core_mut(id) {
			core.set_tag(TAG_REFLECTION_PROBE, true);
		}
	}

	pub fn is_probe_volume(&self, id: ObjectId) -> bool {
		self.core(id)
			.is_some_and(|core| core.has_tag(TAG_PROBE_VOLUME))
	}

	pub fn is_reflection_probe(&self, id: ObjectId) -> bool {
		self.core(id)
			.is_some_and(|core| core.has_tag(TAG_REFLECTION_PROBE))
	}

	pub fn set_physics_enabled(&mut self, id: ObjectId, enabled: bool) -> bool {
		let Some(body) = self
			.collider_of(id)
			.and_then(|collider| self.colliders.get(collider))
			.and_then(|collider| collider.body)
		else {
			return false;
		};

		if enabled {
			self.physics.activate(body);
		} else {
			self.physics.deactivate(body);
		}

		if let Some(core) = self.core_mut(id) {
			core.set_flag(FLAG_PHYSICS_ENABLED, enabled);
		}

		true
	}

	pub fn on_attached(&mut self, id: ObjectId) {
		let Some(body) = self
			.collider_of(id)
			.and_then(|collider| self.colliders.get(collider))
			.and_then(|collider| collider.body)
		else {
			return;
		};

		let active = self.physics.is_active(body);

		self.set_physics_enabled(id, active);
	}

	pub fn set_bounds(&mut self, id: ObjectId, bounds: Aabb) {
		if let Some(core) = self.core_mut(id) {
			store_bounds(core, bounds);
		}

		let mut node = id;

		loop {
			self.update_in_grid(node, false);

			let Some(parent) = self.core(node).map(|core| core.parent_id) else {
				break;
			};

			if parent == NO_ID || !self.is_used(parent) {
				break;
			}

			let (Some(parent_bounds), Some(node_bounds)) = (self.bounds(parent), self.bounds(node))
			else {
				break;
			};

			let (Some(parent_world), Some(node_world)) =
				(self.world_matrix(parent), self.world_matrix(node))
			else {
				break;
			};

			let merged =
				merge_child_bounds(&parent_bounds, &parent_world, &node_bounds, &node_world);

			if let Some(core) = self.core_mut(parent) {
				store_bounds(core, merged);
			}

			node = parent;
		}
	}

	pub fn merge_into_parent(&mut self, id: ObjectId) {
		let Some(parent) = self.core(id).map(|core| core.parent_id) else {
			return;
		};

		if parent == NO_ID || !self.is_used(parent) {
			return;
		}

		if let (Some(parent_bounds), Some(child_bounds)) = (self.bounds(parent), self.bounds(id))
			&& let Some(core) = self.core_mut(parent)
		{
			let mut merged = parent_bounds;
			merged.add(&child_bounds);
			store_bounds(core, merged);
		}
	}

	pub fn raycast_bounds(
		&mut self,
		id: ObjectId,
		origin: [f32; 3],
		direction: [f32; 3],
	) -> Option<(f32, [f32; 3])> {
		let bounds = self.bounds(id)?;
		let matrix = self.world_matrix(id)?;

		let (distance, face) = raycast_bounds(
			&bounds,
			&matrix,
			Vec3f::new(origin[0], origin[1], origin[2]),
			Vec3f::new(direction[0], direction[1], direction[2]),
		);

		(distance >= 0.0).then_some((distance, [face.x, face.y, face.z]))
	}

	pub fn contains_point(&mut self, id: ObjectId, point: [f32; 3]) -> bool {
		let (Some(bounds), Some(matrix)) = (self.bounds(id), self.world_matrix(id)) else {
			return false;
		};

		contains_point(&bounds, &matrix, Vec3f::new(point[0], point[1], point[2]))
	}

	pub fn direction_scale(&self, id: ObjectId, direction: [f32; 3]) -> f32 {
		let (Some(bounds), Some(scale)) = (self.bounds(id), self.scale(id)) else {
			return 0.0;
		};

		direction_scale(
			&bounds,
			scale,
			Vec3f::new(direction[0], direction[1], direction[2]),
		)
	}

	pub fn can_be_frustum_culled(&self, id: ObjectId) -> bool {
		let Some(core) = self.core(id) else {
			return false;
		};

		let bounds = aabb_of(core);

		can_be_frustum_culled(
			&CullInputs {
				cullable: core.is_cullable(),
				has_mesh: core.has_flag(FLAG_HAS_MESH),
				world_layer: core.layer == LAYER_WORLD,
				skinned: core.has_flag(FLAG_SKINNED),
				instance_slots_in_use: u32::from(core.instance_slots_in_use),
				is_instance: core.has_flag(FLAG_IS_INSTANCE),
				physics_enabled: core.has_flag(FLAG_PHYSICS_ENABLED),
			},
			&bounds,
		)
	}

	pub fn update(&mut self, id: ObjectId) {
		let enabled = self
			.core(id)
			.is_some_and(|core| core.has_flag(FLAG_PHYSICS_ENABLED));

		if enabled && let Some(collider) = self.collider_of(id) {
			let (position, rotation, physics_stale) = match self.entity(id) {
				Some(entity) => (
					[entity.position[0], entity.position[1], entity.position[2]],
					entity.rotation,
					entity.is_physics_out_of_date(),
				),
				None => return,
			};

			if physics_stale {
				self.colliders
					.teleport(collider, &mut self.physics, position, rotation);

				if let Some(entity) = self.entity_mut(id) {
					entity.set_physics_out_of_date(false);
				}
			}

			self.sync_with_physics(id, collider);
		}

		if self
			.entity(id)
			.is_some_and(EntityCore::is_matrix_out_of_date)
		{
			self.update_in_grid(id, true);
		}
	}

	fn sync_with_physics(&mut self, id: ObjectId, collider: ColliderId) {
		let Some((body_position, body_rotation)) =
			self.colliders.position_rotation(collider, &self.physics)
		else {
			return;
		};

		let midpoint = self
			.colliders
			.get(collider)
			.map_or([0.0; 3], |collider| collider.midpoint);

		let position = [
			body_position[0] - midpoint[0],
			body_position[1] - midpoint[1],
			body_position[2] - midpoint[2],
		];

		let (Some(current_position), Some(current_rotation)) =
			(self.position(id), self.rotation(id))
		else {
			return;
		};

		if close(current_position, position) && quat_close(current_rotation, body_rotation) {
			return;
		}

		if let Some(entity) = self.entity_mut(id) {
			entity.position = [position[0], position[1], position[2], 0.0];
			entity.rotation = body_rotation;
			entity.mark_matrix_out_of_date();
		}
	}

	pub fn destroy_object(&mut self, id: ObjectId) {
		if !self.is_used(id) {
			return;
		}

		self.destroy_contents(id);

		self.grid.remove_object(id);
		self.nodes[id as usize] = None;
		self.objects.free(id);
	}

	fn destroy_contents(&mut self, id: ObjectId) {
		if let Some(collider) = self.collider_of(id) {
			self.colliders.destroy(collider, &mut self.physics);

			if let Some(core) = self.core_mut(id) {
				core.physics_id = NO_BODY;
			}
		}

		for child in self.children_of(id) {
			self.destroy_contents(child);
		}

		if let Some(core) = self.core_mut(id) {
			core.set_flag(
				FLAG_READY_TO_RENDER | FLAG_IS_INSTANCE | FLAG_PHYSICS_ENABLED,
				false,
			);
		}
	}

	pub fn release_all(&mut self) {
		for id in self.used_ids() {
			self.destroy_object(id);
		}
	}

	pub fn clone_with_own_skeleton(
		&mut self,
		id: ObjectId,
		name: &str,
		clone_skeleton: &mut dyn FnMut(SkeletonRef) -> SkeletonRef,
	) -> Option<ObjectId> {
		let mut cloned: Vec<(SkeletonRef, SkeletonRef)> = Vec::new();

		self.clone_node(id, name, clone_skeleton, &mut cloned)
	}

	fn clone_node(
		&mut self,
		id: ObjectId,
		name: &str,
		clone_skeleton: &mut dyn FnMut(SkeletonRef) -> SkeletonRef,
		cloned: &mut Vec<(SkeletonRef, SkeletonRef)>,
	) -> Option<ObjectId> {
		let source_core = self.core(id)?.clone();
		let source_entity = self.entity(id)?;

		let (position, rotation, scale) = (
			source_entity.position,
			source_entity.rotation,
			source_entity.scale,
		);

		let mesh = self.mesh(id);
		let skeleton = self.node(id)?.skeleton;

		let clone = self.new_object(name, source_core.material_id, source_core.tags)?;

		self.set_mesh(clone, mesh);

		if let Some(core) = self.core_mut(clone) {
			core.bounds_min = source_core.bounds_min;
			core.bounds_max = source_core.bounds_max;
			core.layer = source_core.layer;
			core.flags = source_core.flags;
			core.set_flag(
				FLAG_READY_TO_RENDER | FLAG_IS_INSTANCE | FLAG_PHYSICS_ENABLED,
				false,
			);
			core.set_flag(FLAG_SHARED_MESH, true);
		}

		if let Some(entity) = self.entity_mut(clone) {
			entity.position = position;
			entity.rotation = rotation;
			entity.scale = scale;
			entity.mark_transform_out_of_date();
		}

		if let Some(skeleton) = skeleton {
			let mapped = match cloned.iter().find(|(source, _)| *source == skeleton) {
				Some((_, mapped)) => *mapped,
				None => {
					let mapped = clone_skeleton(skeleton);
					cloned.push((skeleton, mapped));
					mapped
				}
			};

			if let Some(node) = self.node_mut(clone) {
				node.skeleton = Some(mapped);
			}
		}

		for child in source_core.children() {
			if !self.is_used(*child) {
				continue;
			}

			let child_name = self.name(*child).to_owned();

			if let Some(child_clone) = self.clone_node(*child, &child_name, clone_skeleton, cloned)
			{
				if let Some(core) = self.core_mut(child_clone) {
					core.parent_id = clone;
				}

				if let Some(core) = self.core_mut(clone) {
					core.add_child(child_clone);
				}
			}
		}

		Some(clone)
	}

	pub fn attach_loaded(&mut self, id: ObjectId) {
		self.on_attached(id);
		self.mark_added_to_world(id);
		self.add_to_grid(id);
	}

	pub fn detach(&mut self, id: ObjectId) {
		self.remove_from_grid(id);

		if let Some(node) = self.node_mut(id) {
			node.added_to_world = false;
		}
	}

	pub fn mark_added_to_world(&mut self, id: ObjectId) {
		if let Some(node) = self.node_mut(id) {
			node.added_to_world = true;
		}
	}

	pub fn grid_tile(&self, id: ObjectId) -> u32 {
		self.grid.object_tile(id)
	}

	pub fn in_grid(&self, id: ObjectId) -> bool {
		self.grid.object_tile(id) != NULL_TILE
	}
}

#[cfg(test)]
mod tests {
	use raptor_physics::{BodyProps, Motion};

	use super::*;

	fn scene() -> Scene<JoltBackend> {
		let mut scene = Scene::new(JoltBackend::new().unwrap());

		scene.grid.create([8, 8]);

		scene
	}

	fn with_bounds(scene: &mut Scene<JoltBackend>, name: &str, half: f32) -> ObjectId {
		let id = scene.new_object(name, 0, 0).unwrap();

		scene.set_bounds(id, Aabb::new(Vec3f::splat(-half), Vec3f::splat(half)));

		id
	}

	#[test]
	fn objects_are_found_by_name_and_freed() {
		let mut scene = scene();

		let id = scene.new_object("crate", 3, 0).unwrap();

		assert_eq!(scene.find_by_name("crate"), Some(id));
		assert_eq!(scene.core(id).unwrap().material_id, 3);

		scene.destroy_object(id);

		assert!(!scene.is_used(id));
		assert_eq!(scene.find_by_name("crate"), None);
	}

	#[test]
	fn moving_a_parent_moves_what_is_attached() {
		let mut scene = scene();

		let parent = with_bounds(&mut scene, "parent", 1.0);
		let child = with_bounds(&mut scene, "child", 0.5);

		scene.attach_object(parent, child);
		scene.set_position(parent, [4.0, 0.0, 0.0]);

		assert_eq!(scene.position(child), Some([4.0, 0.0, 0.0]));
		assert_eq!(scene.core(child).unwrap().parent_id, parent);
	}

	#[test]
	fn turning_a_parent_turns_what_is_attached() {
		let mut scene = scene();

		let parent = with_bounds(&mut scene, "parent", 1.0);
		let child = with_bounds(&mut scene, "child", 0.5);

		scene.attach_object(parent, child);
		scene.rotate_by_axis(parent, [0.0, 1.0, 0.0], 1.0);

		let parent_rotation = scene.rotation(parent).unwrap();
		let child_rotation = scene.rotation(child).unwrap();

		assert!(quat_close(parent_rotation, child_rotation));
	}

	#[test]
	fn an_object_enters_and_leaves_the_grid_with_its_bounds() {
		let mut scene = scene();

		let id = with_bounds(&mut scene, "box", 0.5);

		scene.add_to_grid(id);

		assert!(scene.in_grid(id));

		scene.set_position(id, [25.0, 0.0, 25.0]);

		assert!(scene.in_grid(id));

		scene.remove_from_grid(id);

		assert!(!scene.in_grid(id));
	}

	#[test]
	fn a_dynamic_body_drives_its_object() {
		let mut scene = scene();

		let floor_collider = scene.colliders.create("floor").unwrap();

		scene
			.colliders
			.create_box(
				floor_collider,
				&mut scene.physics,
				[40.0, 1.0, 40.0],
				Motion::Static,
				&BodyProps::default(),
			)
			.unwrap();

		scene.colliders.teleport(
			floor_collider,
			&mut scene.physics,
			[0.0, -0.5, 0.0],
			[0.0, 0.0, 0.0, 1.0],
		);

		let id = with_bounds(&mut scene, "ball", 0.5);
		let collider = scene.colliders.create("ball").unwrap();

		scene
			.colliders
			.create_box(
				collider,
				&mut scene.physics,
				[1.0; 3],
				Motion::Dynamic,
				&BodyProps::default(),
			)
			.unwrap();

		scene.attach_collider(id, collider);
		scene.set_position(id, [0.0, 5.0, 0.0]);
		scene.set_physics_enabled(id, true);

		for _ in 0..180 {
			scene.physics.update();
			scene.update(id);
		}

		let position = scene.position(id).unwrap();

		assert!(position[1] < 1.0, "{position:?}");
	}

	#[test]
	fn probe_volumes_are_markers_and_not_geometry() {
		let mut scene = scene();

		let id = with_bounds(&mut scene, "volume", 1.0);

		scene.set_shadow_caster(id, true);
		scene.set_probe_volume(id, true);

		assert!(scene.is_probe_volume(id));
		assert!(!scene.core(id).unwrap().is_probe_visible());
		assert!(!scene.core(id).unwrap().has_flag(FLAG_SHADOW_CASTER));

		scene.set_probe_volume(id, false);

		assert!(scene.core(id).unwrap().is_probe_visible());
	}

	#[test]
	fn a_clone_shares_the_mesh_and_gets_its_own_skeleton() {
		let mut scene = scene();

		let parent = with_bounds(&mut scene, "model", 1.0);
		let child = with_bounds(&mut scene, "part", 0.5);

		scene.set_mesh(
			child,
			Some(MeshRef {
				id: 7,
				skinned: true,
			}),
		);
		scene.node_mut(child).unwrap().skeleton = Some(SkeletonRef(1));
		scene.attach_object(parent, child);

		let mut made = 0;

		let clone = scene
			.clone_with_own_skeleton(parent, "model_copy", &mut |_| {
				made += 1;
				SkeletonRef(100 + made)
			})
			.unwrap();

		let clone_child = scene.core(clone).unwrap().children()[0];

		assert_eq!(
			scene.mesh(clone_child),
			Some(MeshRef {
				id: 7,
				skinned: true
			})
		);
		assert_eq!(
			scene.node(clone_child).unwrap().skeleton,
			Some(SkeletonRef(101))
		);
		assert!(scene.core(clone_child).unwrap().has_flag(FLAG_SHARED_MESH));
		assert_eq!(scene.core(clone_child).unwrap().parent_id, clone);
	}

	#[test]
	fn setting_bounds_grows_the_parent() {
		let mut scene = scene();

		let parent = with_bounds(&mut scene, "parent", 1.0);
		let child = with_bounds(&mut scene, "child", 0.5);

		scene.attach_object(parent, child);
		scene.set_position(child, [5.0, 0.0, 0.0]);
		scene.set_bounds(child, Aabb::new(Vec3f::splat(-0.5), Vec3f::splat(0.5)));

		assert!(scene.bounds(parent).unwrap().max.x >= 5.0);
	}
}
