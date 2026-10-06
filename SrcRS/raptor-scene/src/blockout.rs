use std::collections::HashMap;
use std::path::Path;

use raptor_brush::edit::{
	FaceTextureEdit, brush_planes, clip_pieces, edit_face_texture, keep_in_place_offset, move_face,
	quat_from_array, same_planes, save_shape, world_box,
};
use raptor_brush::{Brush, FaceTexture, MAX_PLANES, Plane};
use raptor_level::MaterialRegistry;
use raptor_level::access::FsHost;
use raptor_level::blockout::Block;
use raptor_level::blockout::{
	BlockOut, BrushOut, BrushSource, CameraOut, Level, LevelOut, LightOut, PlaneOut, Rotation,
	SunOut, Texture,
};
use raptor_math::quat_platform as q;
use raptor_math::{Aabb, Vec3f};
use raptor_mesh::Mesh;
use raptor_physics::{Backend, BodyProps, Motion};

use crate::colliders::ColliderId;
use crate::scene::{MeshRef, ObjectId, Scene};

pub const MIN_THICKNESS: f32 = 0.1;
pub const DEFAULT_MATERIAL_INDEX: i32 = 0;
pub const EDITABLE_MATERIAL_INDEX: i32 = 1;
pub const TAG_BLOCKOUT: u32 = 1 << 0;
pub const TAG_LOCK_TRANSFORM: u32 = 1 << 1;
pub const TAG_BLEEDS: u32 = 1 << 4;

pub const MATERIAL_LIST_PATH: &str = "RaptorData/Data/materials/list.prx";
pub const MAIN_CONFIG_PATH: &str = "Config/Main.conf";
pub const CONSTANTS_PATH: &str = "Config/Internal/Constants.conf";

const HIDDEN_POSITION: f32 = -200.0;
const TRANSFORM_MARKER_POSITION: f32 = 200.0;

/// The parts of the game that a blockout is made of but does not own.
pub trait BlockoutEnv {
	fn load_materials(&mut self) -> MaterialRegistry;

	fn create_selection_material(&mut self) -> u32;

	fn upload_mesh(&mut self, mesh: &Mesh) -> MeshRef;

	fn object_attached(&mut self, object: ObjectId);

	fn object_detached(&mut self, object: ObjectId);

	/// Applies the sun, the lights and the camera settings of a level that has just been read.
	fn apply_environment(&mut self, level: &Level, grid: &mut raptor_world::WorldGrid);

	/// The sun, lights and camera settings to put in a level file.
	fn capture_environment(&mut self) -> (Option<SunOut>, Vec<LightOut>, CameraOut);

	fn forget_editor_objects(&mut self);

	/// The material the editor holds back from a selected object, if it has one.
	fn stored_material(&self, object: ObjectId) -> Option<u32>;
}

fn vec(values: [f32; 3]) -> Vec3f {
	Vec3f::from_array(values)
}

fn planes_of(texture: Option<Texture>) -> FaceTexture {
	texture.map_or_else(FaceTexture::default, |texture| FaceTexture {
		offset: texture.offset,
		scale: texture.scale,
		rotation: texture.rotation,
	})
}

fn block_planes(block: &Block) -> Vec<Plane> {
	match &block.brush {
		BrushSource::Box { min, max } => Brush::from_box(vec(*min), vec(*max)).planes,
		BrushSource::Planes {
			planes,
			has_textures,
		} => {
			let listed: Vec<Plane> = planes
				.iter()
				.take(MAX_PLANES)
				.map(|plane| Plane {
					normal: vec(plane.normal),
					distance: plane.distance,
					texture: planes_of(plane.texture),
				})
				.collect();

			brush_planes(&listed, *has_textures)
		}
		BrushSource::Invalid => Vec::new(),
	}
}

fn block_rotation(rotation: &Rotation) -> [f32; 4] {
	match rotation {
		Rotation::Identity => [0.0, 0.0, 0.0, 1.0],
		Rotation::Euler(angles) => q::get_values(q::from_euler_angles(
			Vec3f::new(angles[0], angles[1], angles[2]).0,
		)),
		Rotation::Quat(values) => *values,
	}
}

#[derive(Default)]
pub struct Blockout {
	pub objects: Vec<ObjectId>,
	pub materials: MaterialRegistry,
	pub selection_material: u32,
	pub transform_marker: Option<ObjectId>,
	pub preview: Option<ObjectId>,
	brushes: HashMap<ObjectId, Brush>,
	preview_planes: Vec<Plane>,
}

impl Blockout {
	pub fn new() -> Self {
		Self::default()
	}

	pub fn brush(&self, object: ObjectId) -> Option<&Brush> {
		self.brushes.get(&object)
	}

	pub fn material_for_index(&self, index: i32) -> u32 {
		let material = self.materials.material(index);

		if material == 0 {
			self.default_material()
		} else {
			material
		}
	}

	pub fn index_for_material(&self, material: u32) -> i32 {
		self.materials.find(material)
	}

	pub fn default_material(&self) -> u32 {
		self.materials.material(DEFAULT_MATERIAL_INDEX)
	}

	fn attach<B: Backend>(scene: &mut Scene<B>, env: &mut dyn BlockoutEnv, object: ObjectId) {
		scene.attach_loaded(object);
		env.object_attached(object);
	}

	pub fn create<B: Backend>(&mut self, scene: &mut Scene<B>, env: &mut dyn BlockoutEnv) {
		self.materials = env.load_materials();
		self.selection_material = env.create_selection_material();

		let marker = scene.new_object(
			"PROTO_XFORM",
			self.selection_material,
			TAG_BLOCKOUT | TAG_LOCK_TRANSFORM,
		);

		if let Some(marker) = marker {
			scene.set_probe_visible(marker, false);

			let brush = Brush::from_box(Vec3f::splat(-0.25), Vec3f::splat(0.25));
			let mesh = env.upload_mesh(&brush.generate_mesh());

			scene.set_mesh(marker, Some(mesh));
			scene.set_bounds(marker, Aabb::new(brush.bounds_min, brush.bounds_max));

			Self::attach(scene, env, marker);

			scene.set_position(marker, [TRANSFORM_MARKER_POSITION; 3]);
		}

		self.transform_marker = marker;

		let preview = scene.new_object(
			"PROTO_PREVIEW",
			self.selection_material,
			TAG_BLOCKOUT | TAG_LOCK_TRANSFORM,
		);

		if let Some(preview) = preview {
			scene.set_probe_visible(preview, false);

			let brush = Brush::from_box(Vec3f::splat(-0.25), Vec3f::splat(0.25));
			let mesh = env.upload_mesh(&brush.generate_mesh());

			scene.set_mesh(preview, Some(mesh));
			self.preview_planes = brush.planes.clone();

			Self::attach(scene, env, preview);
		}

		self.preview = preview;

		self.hide_preview(scene);
	}

	fn motion_of<B: Backend>(scene: &Scene<B>, object: ObjectId) -> Motion {
		scene
			.collider_of(object)
			.and_then(|collider| scene.colliders.get(collider))
			.map_or(Motion::Static, |collider| collider.motion)
	}

	pub fn is_dynamic<B: Backend>(&self, scene: &Scene<B>, object: ObjectId) -> bool {
		Self::motion_of(scene, object) == Motion::Dynamic
	}

	fn apply_brush<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		object: ObjectId,
		brush: Brush,
		motion: Motion,
	) {
		let mesh = env.upload_mesh(&brush.generate_mesh());

		scene.set_mesh(object, Some(mesh));

		let (min, max) = (brush.bounds_min, brush.bounds_max);

		if let Some(core) = scene.core_mut(object) {
			core.bounds_min = [min.x, min.y, min.z, 0.0];
			core.bounds_max = [max.x, max.y, max.z, 0.0];
		}

		let midpoint = brush.center();

		if let Some(entity) = scene.entity_mut(object) {
			entity.set_rotation_origin([-midpoint.x, -midpoint.y, -midpoint.z]);
		}

		scene.update_in_grid(object, true);

		if let Some(old) = scene.collider_of(object) {
			scene.colliders.destroy(old, &mut scene.physics);

			if let Some(core) = scene.core_mut(object) {
				core.physics_id = raptor_entity::object_core::NO_BODY;
			}
		}

		let hull: Vec<[f32; 3]> = brush
			.vertices
			.iter()
			.map(|vertex| (*vertex - midpoint).to_array())
			.collect();

		let name = scene.name(object).to_owned();

		if let Some(collider) = scene.colliders.create(&name) {
			let created = scene.colliders.create_hull(
				collider,
				&mut scene.physics,
				&hull,
				motion,
				&BodyProps {
					convex_radius: 0.05,
					density: 20.0,
					..BodyProps::default()
				},
			);

			if let Err(error) = created {
				raptor_core::log_error!(
					"Could not make the collider of blockout '{name}': {error:?}"
				);
			}

			if let Some(entry) = scene.colliders.get_mut(collider) {
				entry.midpoint = midpoint.to_array();
			}

			let position = scene.position(object).unwrap_or([0.0; 3]);
			let rotation = scene.rotation(object).unwrap_or([0.0, 0.0, 0.0, 1.0]);

			scene
				.colliders
				.teleport(collider, &mut scene.physics, position, rotation);

			scene.attach_collider(object, ColliderId(collider.0));
		}

		if scene.is_probe_volume(object) {
			scene.set_probe_volume(object, true);
		}

		self.brushes.insert(object, brush);
	}

	pub fn apply_brush_in_place<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		object: ObjectId,
		brush: Brush,
	) {
		if let Some(current) = self.brushes.get(&object) {
			let rotation = quat_from_array(scene.rotation(object).unwrap_or([0.0, 0.0, 0.0, 1.0]));
			let offset = keep_in_place_offset(&rotation, current.center(), brush.center());

			if !offset.is_close_to(&Vec3f::ZERO, 1.0e-5)
				&& let Some(position) = scene.position(object)
			{
				scene.set_position(
					object,
					[
						position[0] + offset.x,
						position[1] + offset.y,
						position[2] + offset.z,
					],
				);
			}
		}

		let motion = Self::motion_of(scene, object);

		self.apply_brush(scene, env, object, brush, motion);
	}

	pub fn move_face<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		object: ObjectId,
		face_normal: [f32; 3],
		distance: f32,
	) -> bool {
		let Some(brush) = self.brushes.get(&object) else {
			return false;
		};

		let Some(moved) = move_face(&brush.planes, vec(face_normal), distance, MIN_THICKNESS)
		else {
			raptor_core::log_warn!(
				"Cannot move the face of blockout '{}' facing {face_normal:?} that far",
				scene.name(object)
			);
			return false;
		};

		self.apply_brush_in_place(scene, env, object, Brush::from_planes(&moved));

		true
	}

	pub fn set_brush_planes<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		object: ObjectId,
		planes: &[Plane],
	) -> bool {
		let brush = Brush::from_planes(planes);

		if !brush.is_valid() {
			raptor_core::log_error!(
				"Cannot set the planes of blockout '{}', they do not make a valid brush",
				scene.name(object)
			);
			return false;
		}

		self.apply_brush_in_place(scene, env, object, brush);

		true
	}

	pub fn get_clip_pieces<B: Backend>(
		&self,
		scene: &mut Scene<B>,
		object: ObjectId,
		point_a: [f32; 3],
		point_b: [f32; 3],
		face_normal: [f32; 3],
	) -> Option<raptor_brush::edit::ClipPieces> {
		let brush = self.brushes.get(&object)?;
		let to_local = scene.world_matrix(object)?.inverse();
		let rotation = quat_from_array(scene.rotation(object)?);
		let position = vec(scene.position(object)?);

		clip_pieces(
			&brush.planes,
			&to_local,
			&rotation,
			position,
			vec(point_a),
			vec(point_b),
			vec(face_normal),
		)
	}

	pub fn make_world_box(&self, min: [f32; 3], max: [f32; 3]) -> (Brush, [f32; 3]) {
		let (planes, position) = world_box(vec(min), vec(max));

		(Brush::from_planes(&planes), position.to_array())
	}

	pub fn face_texture_edit(
		&self,
		object: ObjectId,
		face_normal: [f32; 3],
		edit: FaceTextureEdit,
		amount: [f32; 2],
	) -> Option<Vec<Plane>> {
		let brush = self.brushes.get(&object)?;

		edit_face_texture(&brush.planes, vec(face_normal), edit, amount)
	}

	pub fn show_preview<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		position: [f32; 3],
		rotation: [f32; 4],
		brush: &Brush,
	) {
		let Some(preview) = self.preview else {
			return;
		};

		if !brush.is_valid() {
			self.hide_preview(scene);
			return;
		}

		if !same_planes(&brush.planes, &self.preview_planes) {
			let mesh = env.upload_mesh(&brush.generate_mesh());

			scene.set_mesh(preview, Some(mesh));

			let (min, max) = (brush.bounds_min, brush.bounds_max);

			if let Some(core) = scene.core_mut(preview) {
				core.bounds_min = [min.x, min.y, min.z, 0.0];
				core.bounds_max = [max.x, max.y, max.z, 0.0];
			}

			let center = brush.center();

			if let Some(entity) = scene.entity_mut(preview) {
				entity.set_rotation_origin([-center.x, -center.y, -center.z]);
			}

			self.preview_planes = brush.planes.clone();
		}

		scene.set_rotation(preview, rotation);
		scene.set_position(preview, position);
	}

	pub fn hide_preview<B: Backend>(&self, scene: &mut Scene<B>) {
		if let Some(preview) = self.preview {
			scene.set_position(preview, [HIDDEN_POSITION; 3]);
		}
	}

	pub fn raycast_face<B: Backend>(
		&self,
		scene: &mut Scene<B>,
		object: ObjectId,
		origin: [f32; 3],
		direction: [f32; 3],
	) -> Option<([f32; 3], [f32; 3])> {
		let brush = self.brushes.get(&object)?;
		let to_local = scene.world_matrix(object)?.inverse();

		let local_origin = to_local * raptor_math::Vec4f::new(origin[0], origin[1], origin[2], 1.0);
		let local_direction =
			to_local * raptor_math::Vec4f::new(direction[0], direction[1], direction[2], 0.0);

		let (distance, plane) = brush.raycast(
			Vec3f::new(local_origin.x, local_origin.y, local_origin.z),
			Vec3f::new(local_direction.x, local_direction.y, local_direction.z),
		)?;

		let normal = brush.planes[plane as usize].normal.to_array();

		Some((
			normal,
			[
				origin[0] + direction[0] * distance,
				origin[1] + direction[1] * distance,
				origin[2] + direction[2] * distance,
			],
		))
	}

	pub fn raycast_blockout<B: Backend>(
		&self,
		scene: &mut Scene<B>,
		origin: [f32; 3],
		direction: [f32; 3],
	) -> Option<(ObjectId, [f32; 3])> {
		let mut hits = scene.physics.raycast_objects(origin, direction);

		hits.dedup();

		for body in hits {
			let Some(collider) = scene.colliders.find_by_body(body) else {
				continue;
			};

			let Some(object) = scene.colliders.get(collider).and_then(|entry| entry.object) else {
				continue;
			};

			if let Some((normal, _)) = self.raycast_face(scene, object, origin, direction) {
				return Some((object, normal));
			}
		}

		None
	}

	fn create_brush_object<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		block: &Block,
	) -> Option<ObjectId> {
		raptor_core::log_info!("Adding blockout '{}'", block.name);

		let planes = block_planes(block);

		let brush = if planes.is_empty() {
			Brush::default()
		} else {
			Brush::from_planes(&planes)
		};

		if !brush.is_valid() {
			raptor_core::log_error!(
				"Blockout '{}' does not describe a valid convex brush, skipping",
				block.name
			);
			return None;
		}

		let mut material = self.default_material();
		let mut tags = TAG_BLOCKOUT;

		if block.locked {
			tags |= TAG_LOCK_TRANSFORM;
		} else {
			material = self.materials.material(EDITABLE_MATERIAL_INDEX);
		}

		if let Some(index) = block.material {
			if self.materials.material(index) == 0 {
				raptor_core::log_warn!(
					"Blockout '{}' uses the unknown material {index}",
					block.name
				);
			}

			material = self.material_for_index(index);
		}

		let object = scene.new_object(&block.name, material, tags)?;

		scene.move_by(object, block.position);
		scene.set_shadow_caster(object, true);
		scene.set_rotation(object, block_rotation(&block.rotation));

		if block.probe_volume {
			scene.set_probe_volume(object, true);
		}

		if block.reflection_probe {
			scene.set_reflection_probe(object, true);
		}

		if block.bleeds
			&& let Some(core) = scene.core_mut(object)
		{
			core.set_tag(TAG_BLEEDS, true);
		}

		let motion = if block.dynamic {
			Motion::Dynamic
		} else {
			Motion::Static
		};

		self.apply_brush(scene, env, object, brush, motion);

		Self::attach(scene, env, object);

		self.objects.push(object);

		Some(object)
	}

	pub fn rebuild_object<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		object: ObjectId,
	) {
		let Some(collider) = scene.collider_of(object) else {
			return;
		};

		let Some(motion) = scene.colliders.get(collider).map(|entry| entry.motion) else {
			return;
		};

		let brush = match self.brushes.get(&object) {
			Some(existing) => Brush::from_planes(&existing.planes),
			None => {
				let bounds = scene.bounds(object).unwrap_or_default();

				Brush::from_box(bounds.min, bounds.max)
			}
		};

		if !brush.is_valid() {
			raptor_core::log_error!(
				"Could not rebuild blockout '{}', its brush is invalid",
				scene.name(object)
			);
			return;
		}

		self.apply_brush(scene, env, object, brush, motion);
	}

	fn remove_single<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		object: ObjectId,
	) {
		scene.detach(object);
		env.object_detached(object);

		self.brushes.remove(&object);

		scene.destroy_object(object);
	}

	pub fn destroy_object<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		object: ObjectId,
	) {
		if !scene.is_used(object) {
			return;
		}

		self.objects.retain(|id| *id != object);

		self.remove_single(scene, env, object);
	}

	fn remove_all<B: Backend>(&mut self, scene: &mut Scene<B>, env: &mut dyn BlockoutEnv) {
		for object in std::mem::take(&mut self.objects) {
			if scene.is_used(object) {
				self.remove_single(scene, env, object);
			}
		}

		self.brushes.clear();
	}

	fn next_name(&self) -> String {
		self.objects.len().to_string()
	}

	pub fn new_object<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		position: [f32; 3],
	) -> Option<ObjectId> {
		let name = self.next_name();

		raptor_core::log_info!("Creating new blockout object '{name}'");

		let object = scene.new_object(&name, self.default_material(), TAG_BLOCKOUT)?;

		scene.move_by(object, position);
		scene.set_shadow_caster(object, true);

		self.apply_brush(
			scene,
			env,
			object,
			Brush::from_box(Vec3f::splat(-0.25), Vec3f::splat(0.25)),
			Motion::Static,
		);

		Self::attach(scene, env, object);

		self.objects.push(object);

		Some(object)
	}

	pub fn dupe_object<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		source: ObjectId,
	) -> Option<ObjectId> {
		if !scene.is_used(source) {
			return None;
		}

		let name = self.next_name();

		raptor_core::log_info!("Creating new blockout object '{name}'");

		let material = scene.core(source)?.material_id;
		let dupe = scene.new_object(&name, material, TAG_BLOCKOUT)?;

		let brush = match self.brushes.get(&source) {
			Some(existing) => Brush::from_planes(&existing.planes),
			None => {
				let bounds = scene.bounds(source)?;

				Brush::from_box(bounds.min, bounds.max)
			}
		};

		let motion = Self::motion_of(scene, source);

		scene.set_position(dupe, scene.position(source)?);
		scene.set_rotation(dupe, scene.rotation(source)?);
		scene.set_shadow_caster(dupe, true);

		if scene.is_probe_volume(source) {
			scene.set_probe_volume(dupe, true);
		}

		if scene.is_reflection_probe(source) {
			scene.set_reflection_probe(dupe, true);
		}

		if scene.core(source)?.has_tag(TAG_BLEEDS)
			&& let Some(core) = scene.core_mut(dupe)
		{
			core.set_tag(TAG_BLEEDS, true);
		}

		self.apply_brush(scene, env, dupe, brush, motion);

		Self::attach(scene, env, dupe);

		self.objects.push(dupe);

		Some(dupe)
	}

	#[allow(clippy::too_many_arguments)]
	pub fn restore_object<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		position: [f32; 3],
		planes: &[Plane],
		material: Option<u32>,
		rotation: [f32; 4],
		name: &str,
		is_dynamic: bool,
	) -> Option<ObjectId> {
		let brush = Brush::from_planes(planes);

		if !brush.is_valid() {
			raptor_core::log_error!("Cannot restore blockout '{name}', its brush is invalid");
			return None;
		}

		let base = if name.is_empty() {
			self.next_name()
		} else {
			name.to_owned()
		};

		let mut unique = base.clone();
		let mut suffix = 0;

		while scene.find_by_name(&unique).is_some() {
			unique = format!("{base}_undo{suffix}");
			suffix += 1;
		}

		raptor_core::log_info!("Restoring blockout object '{unique}'");

		let material = material.unwrap_or_else(|| self.materials.material(EDITABLE_MATERIAL_INDEX));

		let object = scene.new_object(&unique, material, TAG_BLOCKOUT)?;

		scene.move_by(object, position);
		scene.set_shadow_caster(object, true);
		scene.set_rotation(object, rotation);

		let motion = if is_dynamic {
			Motion::Dynamic
		} else {
			Motion::Static
		};

		self.apply_brush(scene, env, object, brush, motion);

		Self::attach(scene, env, object);

		self.objects.push(object);

		Some(object)
	}

	pub fn reload_single_object<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		base: &Path,
		path: &str,
		object: ObjectId,
	) {
		if !scene
			.core(object)
			.is_some_and(|core| core.has_tag(TAG_BLOCKOUT))
		{
			return;
		}

		let Some(level) = read_level(base, path) else {
			return;
		};

		if level.has_errors {
			return;
		}

		let name = scene.name(object).to_owned();

		let Some(block) = level.blocks.iter().find(|block| block.name == name) else {
			return;
		};

		self.remove_single(scene, env, object);

		let replacement = self.create_brush_object(scene, env, block);

		self.objects.retain(|id| *id != object);

		let _ = replacement;
	}

	pub fn load<B: Backend>(
		&mut self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		base: &Path,
		path: &str,
	) -> bool {
		let Some(level) = read_level(base, path) else {
			raptor_core::log_error!("Blockout '{path}' could not be loaded or has no 'all' entry");
			return false;
		};

		if level.has_errors {
			return false;
		}

		if !level.has_blocks {
			raptor_core::log_error!("Blockout '{path}' could not be loaded or has no 'all' entry");
			return false;
		}

		env.forget_editor_objects();
		env.apply_environment(&level, &mut scene.grid);

		self.remove_all(scene, env);

		for block in &level.blocks {
			self.create_brush_object(scene, env, block);
		}

		true
	}

	pub fn level_out<B: Backend>(
		&self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
	) -> LevelOut {
		let (sun, lights, camera) = env.capture_environment();

		let mut blocks = Vec::new();

		for object in self.objects.clone() {
			if !scene.is_used(object) {
				continue;
			}

			let Some(position) = scene.position(object) else {
				continue;
			};

			let bounds = scene.bounds(object).unwrap_or_default();
			let rotation = scene.rotation(object).unwrap_or([0.0, 0.0, 0.0, 1.0]);

			let brush = match self.brushes.get(&object) {
				Some(brush) => {
					let shape = save_shape(&brush.planes);

					if shape.as_box {
						BrushOut::Box([
							-bounds.min.x,
							bounds.max.x,
							bounds.max.y,
							-bounds.min.y,
							bounds.max.z,
							-bounds.min.z,
						])
					} else {
						BrushOut::Planes {
							planes: brush
								.planes
								.iter()
								.map(|plane| PlaneOut {
									normal: plane.normal.to_array(),
									distance: plane.distance,
									texture: Texture {
										offset: plane.texture.offset,
										scale: plane.texture.scale,
										rotation: plane.texture.rotation,
									},
								})
								.collect(),
							textures: shape.textures,
						}
					}
				}
				None => BrushOut::Box([
					-bounds.min.x,
					bounds.max.x,
					bounds.max.y,
					-bounds.min.y,
					bounds.max.z,
					-bounds.min.z,
				]),
			};

			let core = scene.core(object);

			let stored = env
				.stored_material(object)
				.or_else(|| core.map(|core| core.material_id));

			let material = stored
				.map(|material| self.index_for_material(material))
				.filter(|index| *index >= 0);

			blocks.push(BlockOut {
				name: scene.name(object).to_owned(),
				position,
				brush,
				rotation,
				locked: core.is_some_and(|core| core.has_tag(TAG_LOCK_TRANSFORM)),
				probe_volume: scene.is_probe_volume(object),
				reflection_probe: scene.is_reflection_probe(object),
				bleeds: core.is_some_and(|core| core.has_tag(TAG_BLEEDS)),
				dynamic: self.is_dynamic(scene, object),
				material,
			});
		}

		LevelOut {
			sun,
			lights,
			camera,
			blocks,
		}
	}

	pub fn save<B: Backend>(
		&self,
		scene: &mut Scene<B>,
		env: &mut dyn BlockoutEnv,
		base: &Path,
		path: &str,
	) -> bool {
		let level = self.level_out(scene, env);
		let text = level.to_text(&mut FsHost);

		if let Err(error) = raptor_level::save::write_atomic(&base.join(path), &text) {
			raptor_core::log_error!("Could not write the blockout '{path}': {error}");
			return false;
		}

		store_last_blockout_path(base, path);

		true
	}
}

pub fn read_level(base: &Path, path: &str) -> Option<Level> {
	let bytes = std::fs::read(base.join(path)).ok()?;

	let constants = base.join(CONSTANTS_PATH);
	let constants = constants.to_string_lossy().into_owned();

	Some(Level::parse(
		&bytes,
		Some(constants.as_bytes()),
		&mut FsHost,
	))
}

fn store_last_blockout_path(base: &Path, path: &str) {
	use raptor_config::model::{Entry, Primitive};

	let config_path = base.join(MAIN_CONFIG_PATH);

	let Ok(bytes) = std::fs::read(&config_path) else {
		raptor_core::log_warn!("Could not update the blockout entry in {MAIN_CONFIG_PATH}");
		return;
	};

	let constants = base.join(CONSTANTS_PATH);
	let constants = constants.to_string_lossy().into_owned();

	let mut host = FsHost;
	let mut parsed = raptor_config::parse(&bytes, Some(constants.as_bytes()), b".conf", &mut host);

	if parsed.has_errors || parsed.entries.is_empty() {
		raptor_core::log_warn!("Could not update the blockout entry in {MAIN_CONFIG_PATH}");
		return;
	}

	let value = Primitive::string(path.as_bytes());

	match parsed
		.entries
		.iter_mut()
		.find(|entry| entry.name == b"blockout")
	{
		Some(entry) => entry.value = value,
		None => parsed.entries.push(Entry {
			name: b"blockout".to_vec(),
			value,
			..Entry::default()
		}),
	}

	let text = raptor_config::writer::format_file(&parsed.entries, &mut host);

	if let Err(error) = raptor_level::save::write_atomic(&config_path, &text) {
		raptor_core::log_warn!("Could not write {MAIN_CONFIG_PATH}: {error}");
	}
}

#[cfg(test)]
mod tests {
	use raptor_jolt::JoltBackend;

	use super::*;

	struct Env {
		meshes: u32,
		attached: Vec<ObjectId>,
		detached: Vec<ObjectId>,
	}

	impl BlockoutEnv for Env {
		fn load_materials(&mut self) -> MaterialRegistry {
			let mut registry = MaterialRegistry::new();

			registry.push("default", 10);
			registry.push("editable", 11);
			registry.push("brick", 12);

			registry
		}

		fn create_selection_material(&mut self) -> u32 {
			99
		}

		fn upload_mesh(&mut self, _mesh: &Mesh) -> MeshRef {
			self.meshes += 1;

			MeshRef {
				id: self.meshes,
				skinned: false,
			}
		}

		fn object_attached(&mut self, object: ObjectId) {
			self.attached.push(object);
		}

		fn object_detached(&mut self, object: ObjectId) {
			self.detached.push(object);
		}

		fn apply_environment(&mut self, _level: &Level, _grid: &mut raptor_world::WorldGrid) {}

		fn capture_environment(&mut self) -> (Option<SunOut>, Vec<LightOut>, CameraOut) {
			(
				None,
				Vec::new(),
				CameraOut {
					aperture: 16.0,
					shutter: 0.01,
					iso: 100.0,
					exposure_ev: 0.0,
				},
			)
		}

		fn forget_editor_objects(&mut self) {}

		fn stored_material(&self, _object: ObjectId) -> Option<u32> {
			None
		}
	}

	fn setup() -> (Scene<JoltBackend>, Blockout, Env) {
		let mut scene = Scene::new(JoltBackend::new().unwrap());

		scene.grid.create([8, 8]);

		let mut env = Env {
			meshes: 0,
			attached: Vec::new(),
			detached: Vec::new(),
		};

		let mut blockout = Blockout::new();

		blockout.create(&mut scene, &mut env);

		(scene, blockout, env)
	}

	#[test]
	fn the_marker_objects_are_made_and_hidden_away() {
		let (scene, blockout, _) = setup();

		let preview = blockout.preview.unwrap();
		let marker = blockout.transform_marker.unwrap();

		assert_eq!(scene.position(preview), Some([HIDDEN_POSITION; 3]));
		assert_eq!(scene.position(marker), Some([TRANSFORM_MARKER_POSITION; 3]));
		assert_eq!(blockout.default_material(), 10);
	}

	#[test]
	fn a_new_blockout_is_a_box_with_a_collider_in_the_grid() {
		let (mut scene, mut blockout, mut env) = setup();

		let object = blockout
			.new_object(&mut scene, &mut env, [2.0, 0.0, 0.0])
			.unwrap();

		assert!(env.attached.contains(&object));
		assert!(scene.in_grid(object));
		assert!(scene.collider_of(object).is_some());
		assert!(blockout.brush(object).unwrap().is_box());
		assert_eq!(scene.position(object), Some([2.0, 0.0, 0.0]));
		assert_eq!(
			scene.core(object).unwrap().tags & TAG_BLOCKOUT,
			TAG_BLOCKOUT
		);
		assert!(!blockout.is_dynamic(&scene, object));
	}

	#[test]
	fn moving_a_face_keeps_the_far_side_where_it_was() {
		let (mut scene, mut blockout, mut env) = setup();

		let object = blockout
			.new_object(&mut scene, &mut env, [0.0, 0.0, 0.0])
			.unwrap();

		assert!(blockout.move_face(&mut scene, &mut env, object, [1.0, 0.0, 0.0], 1.0));

		let brush = blockout.brush(object).unwrap();

		assert!((brush.bounds_max.x - 1.25).abs() < 1e-4);
		assert!((brush.bounds_min.x + 0.25).abs() < 1e-4);

		let world_min = scene.world_aabb(object).unwrap().min.x;

		assert!((world_min + 0.25).abs() < 1e-3, "{world_min}");
	}

	#[test]
	fn a_face_cannot_be_moved_to_nothing() {
		let (mut scene, mut blockout, mut env) = setup();

		let object = blockout.new_object(&mut scene, &mut env, [0.0; 3]).unwrap();

		assert!(blockout.move_face(&mut scene, &mut env, object, [1.0, 0.0, 0.0], -10.0));

		let width = {
			let brush = blockout.brush(object).unwrap();
			brush.bounds_max.x - brush.bounds_min.x
		};

		assert!(width >= MIN_THICKNESS - 1e-4);
	}

	#[test]
	fn a_dupe_copies_the_brush_and_the_position() {
		let (mut scene, mut blockout, mut env) = setup();

		let object = blockout
			.new_object(&mut scene, &mut env, [3.0, 1.0, 0.0])
			.unwrap();

		let dupe = blockout.dupe_object(&mut scene, &mut env, object).unwrap();

		assert_ne!(dupe, object);
		assert_eq!(scene.position(dupe), scene.position(object));
		assert_eq!(
			blockout.brush(dupe).unwrap().planes.len(),
			blockout.brush(object).unwrap().planes.len()
		);
	}

	#[test]
	fn destroying_a_blockout_takes_it_out_of_everything() {
		let (mut scene, mut blockout, mut env) = setup();

		let object = blockout.new_object(&mut scene, &mut env, [0.0; 3]).unwrap();

		blockout.destroy_object(&mut scene, &mut env, object);

		assert!(!scene.is_used(object));
		assert!(blockout.brush(object).is_none());
		assert!(env.detached.contains(&object));
		assert!(!blockout.objects.contains(&object));
	}

	#[test]
	fn raycasting_finds_the_blockout_and_the_face() {
		let (mut scene, mut blockout, mut env) = setup();

		let object = blockout
			.new_object(&mut scene, &mut env, [0.0, 0.0, 5.0])
			.unwrap();

		let hit = blockout.raycast_blockout(&mut scene, [0.0, 0.0, 0.0], [0.0, 0.0, 20.0]);

		let (found, normal) = hit.unwrap();

		assert_eq!(found, object);
		assert_eq!(normal, [0.0, 0.0, -1.0]);
	}

	#[test]
	fn a_saved_level_loads_back_the_same() {
		let (mut scene, mut blockout, mut env) = setup();

		let directory =
			std::env::temp_dir().join(format!("raptor-blockout-{}", std::process::id()));

		std::fs::create_dir_all(directory.join("Config/Internal")).unwrap();
		std::fs::write(
			directory.join(CONSTANTS_PATH),
			b"True = 1\nFalse = 0\nCLight = {\n\tPoint = 0\n\tSpot = 1\n}\n",
		)
		.unwrap();
		std::fs::write(directory.join(MAIN_CONFIG_PATH), b"blockout = \"old\"\n").unwrap();

		let first = blockout
			.new_object(&mut scene, &mut env, [2.0, 0.0, 0.0])
			.unwrap();

		blockout.move_face(&mut scene, &mut env, first, [1.0, 0.0, 0.0], 1.0);

		blockout.new_object(&mut scene, &mut env, [0.0, 4.0, 0.0]);

		assert!(blockout.save(&mut scene, &mut env, &directory, "level.prx"));

		let main = std::fs::read_to_string(directory.join(MAIN_CONFIG_PATH)).unwrap();

		assert!(main.contains("level.prx"), "{main}");

		assert!(blockout.load(&mut scene, &mut env, &directory, "level.prx"));

		assert_eq!(blockout.objects.len(), 2);

		let reloaded = blockout.objects[0];

		assert_eq!(scene.position(reloaded), Some([2.0, 0.0, 0.0]));
		assert!((blockout.brush(reloaded).unwrap().bounds_max.x - 1.25).abs() < 1e-4);

		std::fs::remove_dir_all(directory).ok();
	}
}
