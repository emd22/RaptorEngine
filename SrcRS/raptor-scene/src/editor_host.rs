use raptor_brush::{Brush, Plane};
use raptor_editor::host::{
	BlockSnapshot, BlockoutHost, BodyId as EditorBodyId, MaterialId, ObjectHost,
	ObjectId as EditorObjectId, PhysicsHost, PlayerHost, RayHit,
};
use raptor_entity::object_core::{FLAG_DISABLE_CULLING, FLAG_SHADOW_CASTER, LAYER_PLAYER, NO_ID};
use raptor_math::mat4::Quat;
use raptor_math::quat_platform as q;
use raptor_math::{Aabb, Mat4f, Vec3f};
use raptor_physics::{Backend, BodyId};

use crate::blockout::{TAG_BLEEDS, TAG_BLOCKOUT};
use crate::scene::{FLAG_UNLIT, TAG_REFLECTION_PROBE};
use crate::world::World;

const TRANSFORM_MARKER_HIDDEN: f32 = 200.0;

fn array(value: Vec3f) -> [f32; 3] {
	value.to_array()
}

fn quat_array(value: Quat) -> [f32; 4] {
	[value.x, value.y, value.z, value.w]
}

fn quat_of(value: [f32; 4]) -> Quat {
	Quat {
		x: value[0],
		y: value[1],
		z: value[2],
		w: value[3],
	}
}

impl<B: Backend> PlayerHost for World<B> {
	fn camera_position(&self) -> Vec3f {
		let position = self.player.camera.position;

		Vec3f::new(position[0], position[1], position[2])
	}

	fn camera_forward(&self) -> Vec3f {
		Vec3f::from_array(self.camera_forward())
	}

	fn player_position(&self) -> Vec3f {
		Vec3f::from_array(self.player.position())
	}

	fn teleport_player(&mut self, position: Vec3f) {
		self.player
			.teleport_to(&mut self.scene.physics, array(position));
	}

	fn set_player_speed_multiplier(&mut self, multiplier: f32) {
		self.player.state.speed_multiplier = multiplier;
	}
}

impl<B: Backend> PhysicsHost for World<B> {
	fn raycast(&self, origin: Vec3f, delta: Vec3f) -> Option<RayHit> {
		let hit = self
			.scene
			.physics
			.raycast(array(origin), array(delta), None)?;

		Some(RayHit {
			point: Vec3f::from_array(hit.point),
			normal: Vec3f::from_array(hit.normal),
			body: Some(EditorBodyId(hit.body.0)),
		})
	}

	fn raycast_objects(&self, origin: Vec3f, delta: Vec3f) -> Vec<EditorObjectId> {
		self.scene
			.physics
			.raycast_objects(array(origin), array(delta))
			.into_iter()
			.filter_map(|body| {
				let collider = self.scene.colliders.find_by_body(body)?;

				self.scene.colliders.get(collider)?.object
			})
			.map(EditorObjectId)
			.collect()
	}

	fn body_is_dynamic(&self, body: EditorBodyId) -> bool {
		self.scene.physics.is_dynamic(BodyId(body.0))
	}

	fn body_point_to_local(&self, body: EditorBodyId, point: Vec3f) -> Vec3f {
		Vec3f::from_array(
			self.scene
				.physics
				.point_to_local(BodyId(body.0), array(point)),
		)
	}

	fn body_hold(
		&mut self,
		body: EditorBodyId,
		local_point: Vec3f,
		target: Vec3f,
		stiffness: f32,
		max_speed: f32,
		angular_damping: f32,
	) -> Option<Vec3f> {
		self.scene
			.physics
			.hold(
				BodyId(body.0),
				array(local_point),
				array(target),
				stiffness,
				max_speed,
				angular_damping,
			)
			.map(Vec3f::from_array)
	}
}

impl<B: Backend> ObjectHost for World<B> {
	fn object_exists(&self, id: EditorObjectId) -> bool {
		self.scene.is_used(id.0)
	}

	fn object_ids(&self) -> Vec<EditorObjectId> {
		self.scene
			.used_ids()
			.into_iter()
			.map(EditorObjectId)
			.collect()
	}

	fn object_name(&self, id: EditorObjectId) -> String {
		self.scene.name(id.0).to_owned()
	}

	fn object_position(&self, id: EditorObjectId) -> Vec3f {
		Vec3f::from_array(self.scene.position(id.0).unwrap_or([0.0; 3]))
	}

	fn set_object_position(&mut self, id: EditorObjectId, position: Vec3f) {
		self.scene.set_position(id.0, array(position));
	}

	fn object_rotation(&self, id: EditorObjectId) -> Quat {
		quat_of(self.scene.rotation(id.0).unwrap_or([0.0, 0.0, 0.0, 1.0]))
	}

	fn object_euler(&self, id: EditorObjectId) -> Vec3f {
		let rotation = self.scene.rotation(id.0).unwrap_or([0.0, 0.0, 0.0, 1.0]);

		Vec3f::from_vector(q::get_euler_angles(q::set(
			rotation[0],
			rotation[1],
			rotation[2],
			rotation[3],
		)))
	}

	fn set_object_euler(&mut self, id: EditorObjectId, euler: Vec3f) {
		let rotation = q::get_values(q::from_euler_angles(euler.0));

		self.scene.set_rotation(id.0, rotation);
	}

	fn object_bounds(&self, id: EditorObjectId) -> Aabb {
		self.scene.bounds(id.0).unwrap_or_default()
	}

	fn set_object_bounds(&mut self, id: EditorObjectId, bounds: Aabb) {
		self.scene.set_bounds(id.0, bounds);
	}

	fn object_tags(&self, id: EditorObjectId) -> u32 {
		self.scene.core(id.0).map_or(0, |core| core.tags)
	}

	fn object_flags(&self, id: EditorObjectId) -> u32 {
		self.scene
			.core(id.0)
			.map_or(0, |core| u32::from(core.flags))
	}

	fn set_object_tag(&mut self, id: EditorObjectId, bit: u32, enabled: bool) {
		match bit {
			raptor_entity::object_core::TAG_PROBE_VOLUME => {
				self.scene.set_probe_volume(id.0, enabled);
			}
			TAG_REFLECTION_PROBE => {
				self.scene.set_reflection_probe(id.0, enabled);
			}
			_ => {
				if let Some(core) = self.scene.core_mut(id.0) {
					core.set_tag(bit, enabled);
				}
			}
		}
	}

	fn set_object_flag(&mut self, id: EditorObjectId, bit: u32, enabled: bool) {
		let flag = bit as u16;

		match flag {
			FLAG_DISABLE_CULLING => self.scene.set_cullable(id.0, !enabled),
			FLAG_UNLIT => self.scene.set_unlit(id.0, enabled),
			FLAG_SHADOW_CASTER => self.scene.set_shadow_caster(id.0, enabled),
			_ => {
				if let Some(core) = self.scene.core_mut(id.0) {
					core.set_flag(flag, enabled);
				}
			}
		}
	}

	fn object_children(&self, id: EditorObjectId) -> Vec<EditorObjectId> {
		self.scene
			.core(id.0)
			.map(|core| {
				core.children()
					.iter()
					.copied()
					.map(EditorObjectId)
					.collect()
			})
			.unwrap_or_default()
	}

	fn object_parent(&self, id: EditorObjectId) -> Option<EditorObjectId> {
		let parent = self.scene.core(id.0)?.parent_id;

		(parent != NO_ID && self.scene.is_used(parent)).then_some(EditorObjectId(parent))
	}

	fn object_has_mesh(&self, id: EditorObjectId) -> bool {
		self.scene.mesh(id.0).is_some()
	}

	fn object_is_player_layer(&self, id: EditorObjectId) -> bool {
		self.scene
			.core(id.0)
			.is_some_and(|core| core.layer == LAYER_PLAYER)
	}

	fn object_in_world(&self, id: EditorObjectId) -> bool {
		self.scene
			.node(id.0)
			.is_some_and(|node| node.added_to_world)
	}

	fn object_world_matrix(&self, id: EditorObjectId) -> Mat4f {
		let entity = self.scene.entity(id.0);

		match entity {
			Some(entity) if !entity.is_matrix_out_of_date() => Mat4f::from_rows(&entity.matrix),
			_ => {
				let mut copy = raptor_entity::EntityCore::default();

				if let Some(entity) = entity {
					copy.position = entity.position;
					copy.rotation = entity.rotation;
					copy.scale = entity.scale;
					copy.rotation_origin = entity.rotation_origin;
					copy.transform_mode = entity.transform_mode;
				}

				Mat4f::from_rows(copy.world_matrix())
			}
		}
	}

	fn object_raycast_bounds(
		&self,
		id: EditorObjectId,
		origin: Vec3f,
		direction: Vec3f,
	) -> Option<(f32, Vec3f)> {
		let bounds = self.scene.bounds(id.0)?;
		let matrix = self.object_world_matrix(id);

		let (distance, face) =
			raptor_world::object_logic::raycast_bounds(&bounds, &matrix, origin, direction);

		(distance >= 0.0).then_some((distance, face))
	}

	fn object_contains_point(&self, id: EditorObjectId, point: Vec3f) -> bool {
		let Some(bounds) = self.scene.bounds(id.0) else {
			return false;
		};

		raptor_world::object_logic::contains_point(&bounds, &self.object_world_matrix(id), point)
	}

	fn object_material(&self, id: EditorObjectId) -> MaterialId {
		MaterialId(self.scene.core(id.0).map_or(0, |core| core.material_id))
	}

	fn set_object_material(&mut self, id: EditorObjectId, material: MaterialId) {
		self.scene.set_material(id.0, material.0);
	}

	fn object_has_dynamic_body(&self, id: EditorObjectId) -> bool {
		self.blockout.is_dynamic(&self.scene, id.0)
	}

	fn raycast_probe_volumes(
		&self,
		origin: Vec3f,
		direction: Vec3f,
		range: f32,
	) -> Option<(EditorObjectId, f32)> {
		let mut nearest: Option<(EditorObjectId, f32)> = None;
		let mut nearest_distance = range;

		for id in self.scene.used_ids() {
			if !self.scene.is_probe_volume(id) {
				continue;
			}

			let Some((distance, _)) =
				self.object_raycast_bounds(EditorObjectId(id), origin, direction)
			else {
				continue;
			};

			if distance < nearest_distance {
				nearest = Some((EditorObjectId(id), distance));
				nearest_distance = distance;
			}
		}

		nearest
	}
}

impl<B: Backend> BlockoutHost for World<B> {
	fn has_blockout(&self) -> bool {
		self.blockout.preview.is_some()
	}

	fn brush_planes(&self, id: EditorObjectId) -> Option<Vec<Plane>> {
		self.blockout.brush(id.0).map(|brush| brush.planes.clone())
	}

	fn set_brush_planes(&mut self, id: EditorObjectId, planes: &[Plane]) {
		self.blockout
			.set_brush_planes(&mut self.scene, &mut *self.services, id.0, planes);
	}

	fn rebuild_block(&mut self, id: EditorObjectId) {
		self.blockout
			.rebuild_object(&mut self.scene, &mut *self.services, id.0);
	}

	fn new_block(&mut self, position: Vec3f) -> Option<EditorObjectId> {
		self.blockout
			.new_object(&mut self.scene, &mut *self.services, array(position))
			.map(EditorObjectId)
	}

	fn dupe_block(&mut self, id: EditorObjectId) -> Option<EditorObjectId> {
		self.blockout
			.dupe_object(&mut self.scene, &mut *self.services, id.0)
			.map(EditorObjectId)
	}

	fn restore_block(
		&mut self,
		snapshot: &BlockSnapshot,
		planes: &[Plane],
	) -> Option<EditorObjectId> {
		let restored = self.blockout.restore_object(
			&mut self.scene,
			&mut *self.services,
			array(snapshot.position),
			planes,
			(snapshot.material.0 != 0).then_some(snapshot.material.0),
			quat_array(snapshot.rotation),
			&snapshot.name,
			snapshot.is_dynamic,
		)?;

		if snapshot.is_probe_volume {
			self.scene.set_probe_volume(restored, true);
		}

		if snapshot.is_reflection_probe {
			self.scene.set_reflection_probe(restored, true);
		}

		Some(EditorObjectId(restored))
	}

	fn destroy_block(&mut self, id: EditorObjectId) {
		self.blockout
			.destroy_object(&mut self.scene, &mut *self.services, id.0);
	}

	fn block_is_dynamic(&self, id: EditorObjectId) -> bool {
		self.blockout.is_dynamic(&self.scene, id.0)
	}

	fn default_material(&self) -> MaterialId {
		MaterialId(self.blockout.default_material())
	}

	fn selection_material(&self) -> MaterialId {
		MaterialId(self.blockout.selection_material)
	}

	fn materials(&self) -> Vec<(MaterialId, String)> {
		(0..self.blockout.materials.len())
			.map(|index| {
				(
					MaterialId(self.blockout.materials.material(index as i32)),
					self.blockout.materials.name(index).to_owned(),
				)
			})
			.collect()
	}

	fn show_preview(&mut self, position: Vec3f, rotation: Quat, planes: &[Plane]) {
		let brush = Brush::from_planes(planes);

		self.blockout.show_preview(
			&mut self.scene,
			&mut *self.services,
			array(position),
			quat_array(rotation),
			&brush,
		);
	}

	fn hide_preview(&mut self) {
		self.blockout.hide_preview(&mut self.scene);
	}

	fn set_transform_marker(&mut self, position: Option<Vec3f>) {
		if let Some(marker) = self.blockout.transform_marker {
			let position =
				position.map_or([TRANSFORM_MARKER_HIDDEN; 3], |position| array(position));

			self.scene.set_position(marker, position);
		}
	}

	fn blockout_path(&self) -> Option<String> {
		(!self.blockout_path.is_empty()).then(|| self.blockout_path.clone())
	}

	fn set_blockout_path(&mut self, path: &str) {
		self.blockout_path = path.to_owned();
	}

	fn save_blockout(&mut self, path: &str) -> bool {
		World::save_blockout(self, path)
	}

	fn load_blockout(&mut self, path: &str) -> bool {
		self.blockout_path = path.to_owned();

		self.reload_blockout()
	}
}

#[allow(dead_code)]
const _: (u32, u32) = (TAG_BLOCKOUT, TAG_BLEEDS);
