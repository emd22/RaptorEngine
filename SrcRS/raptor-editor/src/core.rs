use raptor_math::Vec3f;

use crate::geom;
use crate::history::{EditHistory, EditOp, OpResult};
use crate::host::{EditorHost, ObjectId, TAG_BLOCKOUT, TAG_LOCK_TRANSFORM};
use crate::selection::Selection;

pub const SNAP_STEPS: [f32; 4] = [0.05, 0.10, 0.25, 0.50];
pub const ANGLE_SNAP_STEPS: [f32; 4] = [5.0, 15.0, 45.0, 90.0];

pub struct EditorCore {
	pub selection: Selection,
	pub history: EditHistory,
	pub snap_level: i32,
	pub snap_enabled: bool,
}

impl Default for EditorCore {
	fn default() -> Self {
		Self {
			selection: Selection::new(),
			history: EditHistory::new(),
			snap_level: 2,
			snap_enabled: true,
		}
	}
}

impl EditorCore {
	fn snap_index(&self) -> usize {
		self.snap_level.clamp(0, SNAP_STEPS.len() as i32 - 1) as usize
	}

	pub fn snap_step(&self) -> f32 {
		SNAP_STEPS[self.snap_index()]
	}

	pub fn angle_snap_step(&self) -> f32 {
		if self.snap_enabled {
			ANGLE_SNAP_STEPS[self.snap_index()]
		} else {
			0.0
		}
	}

	pub fn snap_to_grid(&self, position: Vec3f) -> Vec3f {
		if self.snap_enabled {
			geom::round_to_step(position, self.snap_step())
		} else {
			position
		}
	}

	pub fn adjust_snap_level(&mut self, direction: i32) {
		self.snap_level = (self.snap_level + direction).clamp(0, SNAP_STEPS.len() as i32 - 1);
	}

	pub fn push(&mut self, host: &mut dyn EditorHost, op: EditOp, group: i32) -> OpResult {
		self.history.push(host, &mut self.selection, op, group)
	}

	pub fn undo(&mut self, host: &mut dyn EditorHost) -> bool {
		self.history.undo(host, &mut self.selection)
	}

	pub fn redo(&mut self, host: &mut dyn EditorHost) -> bool {
		self.history.redo(host, &mut self.selection)
	}

	pub fn select_object(
		&mut self,
		host: &mut dyn EditorHost,
		object: ObjectId,
		append: bool,
	) -> bool {
		if host.object_tags(object) & TAG_LOCK_TRANSFORM != 0 {
			return false;
		}

		if self.selection.contains(object) {
			return true;
		}

		if !append {
			self.selection.clear(host);
		}

		self.selection.add(host, object)
	}

	pub fn clear_selection(&mut self, host: &mut dyn EditorHost) {
		self.selection.clear(host);
	}

	pub fn deselect_models(&mut self, host: &mut dyn EditorHost) {
		let models: Vec<ObjectId> = self
			.selection
			.objects()
			.filter(|object| host.object_tags(*object) & TAG_BLOCKOUT == 0)
			.collect();

		for model in models {
			self.selection.remove(host, model);
		}
	}

	pub fn set_stored_material(
		&mut self,
		host: &mut dyn EditorHost,
		object: ObjectId,
		material: crate::host::MaterialId,
	) {
		self.selection.set_stored_material(host, object, material);
	}

	pub fn forget_objects(&mut self) {
		self.selection.forget();
		self.history.clear();
	}
}
