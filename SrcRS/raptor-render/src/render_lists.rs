use std::collections::HashSet;

use raptor_entity::object_core::{
	FLAG_HAS_MESH, FLAG_IS_INSTANCE, FLAG_PHYSICS_ENABLED, FLAG_SHADOW_CASTER, FLAG_SKINNED,
	LAYER_WORLD, TAG_PROBE_VOLUME,
};
use raptor_math::frustum::{ALL_PLANES, Frustum};
use raptor_math::{Aabb, Mat4f, Obb, Vec3f};
use raptor_world::object_store::ObjectStore;

use crate::object_logic::{CullInputs, can_be_frustum_culled};

/// How many objects the frustum was asked about, and how many it threw out.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CullStats
{
	pub tested: u32,
	pub culled: u32,
}

/// What the lists need from the engine: which pipeline draws a material, and a place to put an
/// object in a pipeline's list.
pub struct ListSinks<'a>
{
	pub pipeline_for_material: &'a mut dyn FnMut(u32) -> u32,
	pub add: &'a mut dyn FnMut(u32, u32),
}

/// Fills the render lists for a frame from the tiles that can be seen. The shadow list holds each
/// object once, however many tiles it spans.
pub struct ListBuilder<'a>
{
	store: &'a ObjectStore,
	frustum: Option<Frustum>,
	shadow_pipeline: u32,
	in_shadow_list: HashSet<u32>,
	pub stats: CullStats,
}

impl<'a> ListBuilder<'a>
{
	pub fn new(store: &'a ObjectStore, frustum: Option<Frustum>, shadow_pipeline: u32) -> Self
	{
		Self {
			store,
			frustum,
			shadow_pipeline,
			in_shadow_list: HashSet::new(),
			stats: CullStats::default(),
		}
	}

	/// Adds the objects of one tile.
	pub fn add_tile(&mut self, objects: &[u32], sinks: &mut ListSinks)
	{
		for &id in objects {
			let Some(handles) = self.store.get(id) else {
				continue;
			};

			// SAFETY: the records last until the object is freed, which does not happen mid-frame.
			let object = unsafe { &*handles.object };

			if object.has_tag(TAG_PROBE_VOLUME) {
				continue;
			}

			if object.has_flag(FLAG_SHADOW_CASTER) {
				self.add_to_shadow_list(id, sinks);
			}

			self.add_by_material(id, sinks);
		}
	}

	fn add_to_shadow_list(&mut self, id: u32, sinks: &mut ListSinks)
	{
		if !self.in_shadow_list.insert(id) {
			return;
		}

		(sinks.add)(self.shadow_pipeline, id);

		let Some(handles) = self.store.get(id) else {
			return;
		};

		// SAFETY: as above.
		for &child in unsafe { &*handles.object }.children() {
			self.add_to_shadow_list(child, sinks);
		}
	}

	/// `None` if the frustum is not asked about the object, otherwise whether the object is out of
	/// its view.
	fn cull_check(&self, id: u32) -> Option<bool>
	{
		let frustum = self.frustum.as_ref()?;
		let handles = self.store.get(id)?;

		// SAFETY: the records last until the object is freed, which does not happen mid-frame.
		let (entity, object) = unsafe { (&mut *handles.entity, &*handles.object) };

		let bounds = Aabb::new(
			Vec3f::new(
				object.bounds_min[0],
				object.bounds_min[1],
				object.bounds_min[2],
			),
			Vec3f::new(
				object.bounds_max[0],
				object.bounds_max[1],
				object.bounds_max[2],
			),
		);

		let inputs = CullInputs {
			cullable: object.is_cullable(),
			has_mesh: object.has_flag(FLAG_HAS_MESH),
			world_layer: object.layer == LAYER_WORLD,
			skinned: object.has_flag(FLAG_SKINNED),
			instance_slots_in_use: u32::from(object.instance_slots_in_use),
			is_instance: object.has_flag(FLAG_IS_INSTANCE),
			physics_enabled: object.has_flag(FLAG_PHYSICS_ENABLED),
		};

		if !can_be_frustum_culled(&inputs, &bounds) {
			return None;
		}

		let world = Mat4f::from_rows(entity.world_matrix());

		Some(!frustum.intersects_obb(&Obb::from_local_bounds(&bounds, &world), ALL_PLANES))
	}

	fn add_by_material(&mut self, id: u32, sinks: &mut ListSinks)
	{
		let Some(handles) = self.store.get(id) else {
			return;
		};

		// SAFETY: as above.
		let object = unsafe { &*handles.object };

		if object.has_flag(FLAG_HAS_MESH) {
			let mut visible = true;

			if let Some(outside) = self.cull_check(id) {
				self.stats.tested += 1;

				if outside {
					self.stats.culled += 1;
					visible = false;
				}
			}

			if visible {
				let pipeline = (sinks.pipeline_for_material)(object.material_id);

				(sinks.add)(pipeline, id);
			}
		}

		for &child in object.children() {
			self.add_by_material(child, sinks);
		}
	}
}

#[cfg(test)]
mod tests
{

	use super::*;

	fn store_with_two_meshes() -> ObjectStore
	{
		let store = ObjectStore::new(8);

		for (index, x) in [0.0f32, 500.0].into_iter().enumerate() {
			let handles = store.alloc(index as u32).unwrap();

			// SAFETY: the records are live.
			unsafe {
				let object = &mut *handles.object;

				object.set_flag(FLAG_HAS_MESH, true);
				object.set_flag(FLAG_SHADOW_CASTER, true);
				object.material_id = 3;
				object.bounds_min = [-1.0, -1.0, -1.0, 0.0];
				object.bounds_max = [1.0, 1.0, 1.0, 0.0];

				(*handles.entity).set_position([x, 0.0, 5.0]);
			}
		}

		store
	}

	fn run(store: &ObjectStore, frustum: Option<Frustum>) -> (Vec<(u32, u32)>, CullStats)
	{
		let mut added = Vec::new();
		let mut pipeline_for_material = |material: u32| 100 + material;
		let mut add = |pipeline: u32, id: u32| added.push((pipeline, id));

		let mut builder = ListBuilder::new(store, frustum, 7);

		builder.add_tile(
			&[0, 1],
			&mut ListSinks {
				pipeline_for_material: &mut pipeline_for_material,
				add: &mut add,
			},
		);

		let stats = builder.stats;

		(added, stats)
	}

	fn looking_down_z() -> Frustum
	{
		let view = Mat4f::look_at(Vec3f::ZERO, Vec3f::new(0.0, 0.0, 1.0), Vec3f::UP);
		let projection = Mat4f::perspective(1.0, 1.0, 100.0, 0.1);

		Frustum::from_view_projection(&(view * projection))
	}

	#[test]
	fn without_a_frustum_every_object_goes_in_its_shadow_and_material_lists()
	{
		let (added, stats) = run(&store_with_two_meshes(), None);

		assert_eq!(added, vec![(7, 0), (100 + 3, 0), (7, 1), (100 + 3, 1)]);
		assert_eq!(stats, CullStats::default());
	}

	#[test]
	fn an_object_far_off_to_the_side_is_culled_and_counted()
	{
		let store = store_with_two_meshes();

		for id in 0..2 {
			// SAFETY: the records are live.
			unsafe { &mut *store.get(id).unwrap().object }.set_flag(FLAG_SHADOW_CASTER, false);
		}

		let (added, stats) = run(&store, Some(looking_down_z()));

		assert_eq!(added, vec![(103, 0)]);
		assert_eq!(
			stats,
			CullStats {
				tested: 2,
				culled: 1
			}
		);
	}

	#[test]
	fn probe_volumes_are_never_drawn()
	{
		let store = store_with_two_meshes();

		// SAFETY: the record is live.
		unsafe { &mut *store.get(0).unwrap().object }.set_tag(TAG_PROBE_VOLUME, true);

		let (added, _) = run(&store, None);

		assert!(added.iter().all(|(_, id)| *id != 0));
	}

	#[test]
	fn children_are_listed_after_their_parent_and_a_shadow_caster_only_once()
	{
		let store = store_with_two_meshes();

		// SAFETY: the record is live.
		unsafe { &mut *store.get(0).unwrap().object }.add_child(1);

		let mut added = Vec::new();
		let mut pipeline_for_material = |material: u32| material;
		let mut add = |pipeline: u32, id: u32| added.push((pipeline, id));

		let mut builder = ListBuilder::new(&store, None, 7);

		builder.add_tile(
			&[0, 1],
			&mut ListSinks {
				pipeline_for_material: &mut pipeline_for_material,
				add: &mut add,
			},
		);

		assert_eq!(
			added.iter().filter(|(pipeline, _)| *pipeline == 7).count(),
			2
		);
		assert_eq!(
			added.iter().filter(|(pipeline, _)| *pipeline == 3).count(),
			3
		);
	}
}
