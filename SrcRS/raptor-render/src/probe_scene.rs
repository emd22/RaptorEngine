use std::path::PathBuf;

use raptor_core::log_warn;
use raptor_entity::object_core::{FLAG_HAS_MESH, TAG_PROBE_VOLUME};
use raptor_math::{Aabb, Mat4f, Obb, Vec3f};
use raptor_world::object_store::ObjectStore;

use crate::probe_data::{self, ReflectionBox};
use crate::probe_placement::{PlacementBox, PlacementBoxes, Plane};
use crate::probes::{ProbeScene, VolumeBrush};

pub const TAG_REFLECTION_PROBE: u32 = 1 << 3;

const MAX_PLACEMENT_OBJECT_SIZE: f32 = 100.0;
const AXIS_EPSILON: f32 = 1e-5;

pub struct StoreProbeScene<'a> {
	pub store: &'a ObjectStore,
	pub is_unlit: &'a dyn Fn(u32) -> bool,
	pub brush_planes: &'a dyn Fn(u32) -> Option<Vec<Plane>>,
	pub name: &'a dyn Fn(u32) -> String,
	pub world_matrix: Option<&'a dyn Fn(u32) -> Mat4f>,
	pub file_path: PathBuf,
}

impl StoreProbeScene<'_> {
	fn matrix(&self, id: u32) -> Option<Mat4f> {
		if let Some(world_matrix) = self.world_matrix {
			return Some(world_matrix(id));
		}

		let handles = self.store.get(id)?;

		// SAFETY: the records last until the object is freed, which does not happen during a
		// bake or a placement.
		let entity = unsafe { &mut *handles.entity };

		Some(Mat4f::from_rows(entity.world_matrix()))
	}

	fn local_bounds(&self, id: u32) -> Option<Aabb> {
		let handles = self.store.get(id)?;

		// SAFETY: as in `matrix`.
		let object = unsafe { &*handles.object };

		let [min_x, min_y, min_z, _] = object.bounds_min;
		let [max_x, max_y, max_z, _] = object.bounds_max;

		Some(Aabb::new(
			Vec3f::new(min_x, min_y, min_z),
			Vec3f::new(max_x, max_y, max_z),
		))
	}

	fn world_bounds(&self, id: u32) -> Option<Aabb> {
		Some(Obb::from_local_bounds(&self.local_bounds(id)?, &self.matrix(id)?).world_aabb())
	}
}

impl ProbeScene for StoreProbeScene<'_> {
	fn placement_boxes(&self) -> PlacementBoxes {
		let mut boxes = PlacementBoxes::default();

		let mut objects = 0;
		let mut kept = 0;

		for id in self.store.used_ids() {
			let Some(handles) = self.store.get(id) else {
				continue;
			};

			// SAFETY: as in `matrix`.
			let object = unsafe { &*handles.object };

			if !object.has_flag(FLAG_HAS_MESH) || (self.is_unlit)(id) || !object.is_probe_visible()
			{
				continue;
			}

			let (Some(local_bounds), Some(local_to_world)) =
				(self.local_bounds(id), self.matrix(id))
			else {
				continue;
			};

			let world_bounds = Obb::from_local_bounds(&local_bounds, &local_to_world).world_aabb();

			if (world_bounds.max - world_bounds.min).length() > MAX_PLACEMENT_OBJECT_SIZE {
				continue;
			}

			let planes = (self.brush_planes)(id).unwrap_or_default();

			let m = local_to_world.to_rows();

			let axis_aligned = planes.is_empty()
				&& [m[1], m[2], m[4], m[6], m[8], m[9]]
					.iter()
					.all(|value| value.abs() < AXIS_EPSILON);

			if boxes.add(PlacementBox {
				local_bounds,
				world_bounds,
				axis_aligned,
				local_to_world,
				world_to_local: local_to_world.inverse(),
				planes,
			}) {
				kept += 1;
			}

			boxes.extend_bounds(world_bounds.min, world_bounds.max);
			objects += 1;
		}

		if objects > kept {
			log_warn!(
				"Probe placement only keeps probes out of the first {kept} of {objects} objects"
			);
		}

		boxes
	}

	fn volume_brushes(&self) -> Vec<VolumeBrush> {
		self.store
			.ids_with_tags(TAG_PROBE_VOLUME)
			.into_iter()
			.filter(|id| {
				self.store.get(*id).is_some_and(|handles| {
					// SAFETY: as in `matrix`.
					!unsafe { &*handles.object }.has_tag(TAG_REFLECTION_PROBE)
				})
			})
			.filter_map(|id| {
				let bounds = self.world_bounds(id)?;

				Some(VolumeBrush {
					name: (self.name)(id),
					min: bounds.min,
					max: bounds.max,
				})
			})
			.collect()
	}

	fn reflection_boxes(&self) -> Vec<ReflectionBox> {
		self.store
			.ids_with_tags(TAG_REFLECTION_PROBE)
			.into_iter()
			.filter_map(|id| {
				let local = self.local_bounds(id)?;

				Some(probe_data::reflection_box(
					local.min,
					local.max,
					&self.matrix(id)?,
				))
			})
			.collect()
	}

	fn probe_file_path(&self) -> PathBuf {
		self.file_path.clone()
	}
}
