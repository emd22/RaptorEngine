use raptor_brush::Plane;
use raptor_brush::edit::move_face;
use raptor_math::{Aabb, Vec3f};

use crate::host::*;
use crate::selection::Selection;

pub const MIN_BLOCKOUT_THICKNESS: f32 = 0.25;
const MAX_OPERATIONS: usize = 256;

pub const EDITABLE_TAGS: [u32; 4] = [
	TAG_LOCK_TRANSFORM,
	TAG_PROBE_VOLUME,
	TAG_REFLECTION_PROBE,
	TAG_BLEEDS,
];
pub const EDITABLE_FLAGS: [u32; 4] = [
	FLAG_PHYSICS_ENABLED,
	FLAG_SHADOW_CASTER,
	FLAG_DISABLE_CULLING,
	FLAG_NOT_PROBE_VISIBLE,
];

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OpResult {
	None,
	Position(Vec3f),
	Object(ObjectId),
	Light(LightId),
}

impl OpResult {
	pub fn object(self) -> Option<ObjectId> {
		match self {
			OpResult::Object(id) => Some(id),
			_ => None,
		}
	}

	pub fn light(self) -> Option<LightId> {
		match self {
			OpResult::Light(id) => Some(id),
			_ => None,
		}
	}
}

#[derive(Clone, Debug)]
pub struct ObjectSnapshot {
	pub block: BlockSnapshot,
	pub planes: Vec<Plane>,
}

#[derive(Clone, Debug)]
pub enum EditOp {
	Move {
		object: ObjectId,
		before: Vec3f,
		after: Vec3f,
	},
	Scale {
		object: ObjectId,
		face: Vec3f,
		amount: f32,
		planes_before: Vec<Plane>,
	},
	Rotate {
		object: ObjectId,
		before: Vec3f,
		after: Vec3f,
	},
	Dupe {
		source: ObjectId,
		origin: Vec3f,
		dupe: Option<ObjectId>,
	},
	Create {
		position: Vec3f,
		object: Option<ObjectId>,
	},
	Delete {
		object: ObjectId,
		snapshot: ObjectSnapshot,
	},
	BrushEdit {
		object: ObjectId,
		before: Vec<Plane>,
		after: Vec<Plane>,
	},
	CreateBrush {
		snapshot: ObjectSnapshot,
		object: Option<ObjectId>,
	},
	LightTransform {
		light: LightId,
		position_before: Vec3f,
		position_after: Vec3f,
		direction_before: Vec3f,
		direction_after: Vec3f,
	},
	LightCreate {
		snapshot: SpotLight,
		light: Option<LightId>,
	},
	LightDelete {
		light: LightId,
		snapshot: SpotLight,
	},
	BoundsEdit {
		object: ObjectId,
		before: Aabb,
		after: Aabb,
	},
	ObjectState {
		object: ObjectId,
		bit: u32,
		is_tag: bool,
		enabled: bool,
		tags_before: u32,
		nodes_before: Vec<(ObjectId, u32)>,
	},
}

pub fn is_blockout(host: &dyn ObjectHost, object: ObjectId) -> bool {
	host.object_tags(object) & TAG_BLOCKOUT != 0
}

pub fn can_edit_tag(host: &dyn ObjectHost, object: ObjectId, bit: u32) -> bool {
	if !host.object_exists(object) {
		return false;
	}

	match bit {
		TAG_LOCK_TRANSFORM | TAG_BLEEDS => true,
		TAG_PROBE_VOLUME | TAG_REFLECTION_PROBE => is_blockout(host, object),
		_ => false,
	}
}

pub fn can_edit_flag(host: &dyn ObjectHost, object: ObjectId, bit: u32) -> bool {
	if !host.object_exists(object) {
		return false;
	}

	let is_probe_volume = host.object_tags(object) & TAG_PROBE_VOLUME != 0;

	match bit {
		FLAG_DISABLE_CULLING => true,
		FLAG_SHADOW_CASTER | FLAG_NOT_PROBE_VISIBLE => !is_probe_volume,
		FLAG_PHYSICS_ENABLED => host.object_has_dynamic_body(object) && !is_probe_volume,
		_ => false,
	}
}

fn for_each_node(host: &dyn ObjectHost, root: ObjectId, func: &mut dyn FnMut(ObjectId)) {
	func(root);

	for child in host.object_children(root) {
		if host.object_exists(child) {
			for_each_node(host, child, func);
		}
	}
}

fn apply_tag(host: &mut dyn EditorHost, object: ObjectId, bit: u32, enabled: bool) {
	if !can_edit_tag(host, object, bit) || (host.object_tags(object) & bit != 0) == enabled {
		return;
	}

	let was_reflection_probe = host.object_tags(object) & TAG_REFLECTION_PROBE != 0;

	host.set_object_tag(object, bit, enabled);

	let is_reflection_probe = host.object_tags(object) & TAG_REFLECTION_PROBE != 0;

	if was_reflection_probe != is_reflection_probe {
		host.rebuild_reflection_probes();
	}
}

fn set_flag(host: &mut dyn EditorHost, object: ObjectId, bit: u32, enabled: bool, recursive: bool) {
	let recursive = recursive && (bit == FLAG_SHADOW_CASTER || bit == FLAG_DISABLE_CULLING);

	if recursive {
		let mut nodes = Vec::new();

		for_each_node(host, object, &mut |node| nodes.push(node));

		for node in nodes {
			host.set_object_flag(node, bit, enabled);
		}
	} else {
		host.set_object_flag(object, bit, enabled);
	}
}

fn apply_flag(host: &mut dyn EditorHost, object: ObjectId, bit: u32, enabled: bool) {
	if !can_edit_flag(host, object, bit) || (host.object_flags(object) & bit != 0) == enabled {
		return;
	}

	set_flag(host, object, bit, enabled, true);
}

fn restore_object_state(
	host: &mut dyn EditorHost,
	root: ObjectId,
	tags_before: u32,
	nodes_before: &[(ObjectId, u32)],
) {
	for bit in EDITABLE_TAGS {
		apply_tag(host, root, bit, tags_before & bit != 0);
	}

	for &(node, flags) in nodes_before {
		if !host.object_exists(node) {
			continue;
		}

		for bit in EDITABLE_FLAGS {
			let wanted = flags & bit != 0;

			if can_edit_flag(host, node, bit) && (host.object_flags(node) & bit != 0) != wanted {
				set_flag(host, node, bit, wanted, false);
			}
		}
	}
}

pub fn capture_object_state(
	host: &dyn ObjectHost,
	object: ObjectId,
) -> (u32, Vec<(ObjectId, u32)>) {
	let mut nodes = Vec::new();

	for_each_node(host, object, &mut |node| {
		nodes.push((node, host.object_flags(node)))
	});

	(host.object_tags(object), nodes)
}

pub fn snapshot_block(
	host: &dyn EditorHost,
	selection: &Selection,
	object: ObjectId,
) -> ObjectSnapshot {
	let tags = host.object_tags(object);

	let planes = host.brush_planes(object).unwrap_or_else(|| {
		let bounds = host.object_bounds(object);

		raptor_brush::Brush::from_box(bounds.min, bounds.max).planes
	});

	ObjectSnapshot {
		block: BlockSnapshot {
			name: host.object_name(object),
			position: host.object_position(object),
			rotation: host.object_rotation(object),
			material: selection.stored_material(host, object),
			is_probe_volume: tags & TAG_PROBE_VOLUME != 0,
			is_reflection_probe: tags & TAG_REFLECTION_PROBE != 0,
			is_dynamic: host.block_is_dynamic(object),
		},
		planes,
	}
}

fn restore_block(host: &mut dyn EditorHost, snapshot: &ObjectSnapshot) -> Option<ObjectId> {
	let object = host.restore_block(&snapshot.block, &snapshot.planes)?;

	if snapshot.block.is_probe_volume {
		host.set_object_tag(object, TAG_PROBE_VOLUME, true);
	}

	if snapshot.block.is_reflection_probe {
		host.set_object_tag(object, TAG_REFLECTION_PROBE, true);
	}

	Some(object)
}

fn alive(host: &dyn EditorHost, object: ObjectId) -> Option<ObjectId> {
	host.object_exists(object).then_some(object)
}

fn alive_light(host: &dyn EditorHost, light: LightId) -> Option<LightId> {
	host.spot_light(light).map(|_| light)
}

fn spawn_light(host: &mut dyn EditorHost, snapshot: &SpotLight) -> Option<LightId> {
	host.create_spot_light(snapshot)
}

impl EditOp {
	pub fn changes_objects(&self) -> bool {
		matches!(
			self,
			EditOp::Dupe { .. }
				| EditOp::Create { .. }
				| EditOp::CreateBrush { .. }
				| EditOp::Delete { .. }
		)
	}

	pub fn remap(&mut self, old: ObjectId, new: ObjectId) {
		let swap = |id: &mut ObjectId| {
			if *id == old {
				*id = new;
			}
		};

		match self {
			EditOp::Move { object, .. }
			| EditOp::Scale { object, .. }
			| EditOp::Rotate { object, .. }
			| EditOp::BrushEdit { object, .. }
			| EditOp::BoundsEdit { object, .. }
			| EditOp::ObjectState { object, .. }
			| EditOp::Delete { object, .. } => swap(object),
			EditOp::Dupe { source, dupe, .. } => {
				swap(source);
				dupe.iter_mut().for_each(swap);
			}
			EditOp::Create { object, .. } | EditOp::CreateBrush { object, .. } => {
				object.iter_mut().for_each(swap)
			}
			_ => {}
		}
	}

	pub fn execute(&mut self, host: &mut dyn EditorHost, selection: &mut Selection) -> OpResult {
		match self {
			EditOp::Move { object, after, .. } => {
				if let Some(target) = alive(host, *object) {
					host.set_object_position(target, *after);
				}

				OpResult::Position(*after)
			}
			EditOp::Scale {
				object,
				face,
				amount,
				planes_before,
			} => {
				if let Some(target) = alive(host, *object)
					&& let Some(planes) = host.brush_planes(target)
				{
					*planes_before = planes.clone();

					if let Some(moved) = move_face(&planes, *face, *amount, MIN_BLOCKOUT_THICKNESS)
					{
						host.set_brush_planes(target, &moved);
					}
				}

				OpResult::None
			}
			EditOp::Rotate { object, after, .. } => {
				if let Some(target) = alive(host, *object) {
					host.set_object_euler(target, *after);
				}

				OpResult::Position(*after)
			}
			EditOp::BoundsEdit { object, after, .. } => {
				if let Some(target) = alive(host, *object) {
					host.set_object_bounds(target, *after);
				}

				OpResult::None
			}
			EditOp::ObjectState {
				object,
				bit,
				is_tag,
				enabled,
				..
			} => {
				if let Some(target) = alive(host, *object) {
					if *is_tag {
						apply_tag(host, target, *bit, *enabled);
					} else {
						apply_flag(host, target, *bit, *enabled);
					}
				}

				OpResult::None
			}
			EditOp::LightTransform {
				light,
				position_after,
				direction_after,
				..
			} => {
				if let Some(target) = alive_light(host, *light) {
					host.set_light_position(target, *position_after);
					host.set_light_direction(target, *direction_after);
				}

				OpResult::Position(*position_after)
			}
			EditOp::LightCreate { snapshot, light } => {
				*light = spawn_light(host, snapshot);

				light.map_or(OpResult::None, OpResult::Light)
			}
			EditOp::LightDelete { light, .. } => {
				if let Some(target) = alive_light(host, *light) {
					host.destroy_light(target);
				}

				OpResult::None
			}
			EditOp::BrushEdit { object, after, .. } => {
				if let Some(target) = alive(host, *object) {
					host.set_brush_planes(target, after);
				}

				OpResult::None
			}
			EditOp::Dupe {
				source,
				origin,
				dupe,
			} => {
				let Some(duped) = host.dupe_block(*source) else {
					return OpResult::None;
				};

				host.set_object_position(duped, *origin);

				let material = selection.stored_material(host, *source);
				host.set_object_material(duped, material);

				*dupe = Some(duped);

				OpResult::Object(duped)
			}
			EditOp::Create { position, object } => {
				*object = host.new_block(*position);

				object.map_or(OpResult::None, OpResult::Object)
			}
			EditOp::CreateBrush { snapshot, object } => {
				*object = restore_block(host, snapshot);

				object.map_or(OpResult::None, OpResult::Object)
			}
			EditOp::Delete { object, .. } => {
				if let Some(target) = alive(host, *object) {
					selection.remove(host, target);
					host.destroy_block(target);
				}

				OpResult::None
			}
		}
	}

	pub fn undo(&mut self, host: &mut dyn EditorHost, selection: &mut Selection) {
		match self {
			EditOp::Move { object, before, .. } => {
				if let Some(target) = alive(host, *object) {
					host.set_object_position(target, *before);
				}
			}
			EditOp::Scale {
				object,
				planes_before,
				..
			} => {
				if let Some(target) = alive(host, *object) {
					host.set_brush_planes(target, planes_before);
				}
			}
			EditOp::Rotate { object, before, .. } => {
				if let Some(target) = alive(host, *object) {
					host.set_object_euler(target, *before);
				}
			}
			EditOp::BoundsEdit { object, before, .. } => {
				if let Some(target) = alive(host, *object) {
					host.set_object_bounds(target, *before);
				}
			}
			EditOp::ObjectState {
				object,
				tags_before,
				nodes_before,
				..
			} => {
				if let Some(target) = alive(host, *object) {
					restore_object_state(host, target, *tags_before, nodes_before);
				}
			}
			EditOp::LightTransform {
				light,
				position_before,
				direction_before,
				..
			} => {
				if let Some(target) = alive_light(host, *light) {
					host.set_light_position(target, *position_before);
					host.set_light_direction(target, *direction_before);
				}
			}
			EditOp::LightCreate { light, .. } => {
				if let Some(target) = light.take().and_then(|id| alive_light(host, id)) {
					host.destroy_light(target);
				}
			}
			EditOp::LightDelete { light, snapshot } => {
				if let Some(restored) = spawn_light(host, snapshot) {
					*light = restored;
				}
			}
			EditOp::BrushEdit { object, before, .. } => {
				if let Some(target) = alive(host, *object) {
					host.set_brush_planes(target, before);
				}
			}
			EditOp::Dupe { dupe, .. } => {
				if let Some(target) = dupe.take().and_then(|id| alive(host, id)) {
					selection.remove(host, target);
					host.destroy_block(target);
				}
			}
			EditOp::Create { object, .. } | EditOp::CreateBrush { object, .. } => {
				if let Some(target) = object.take().and_then(|id| alive(host, id)) {
					selection.remove(host, target);
					host.destroy_block(target);
				}
			}
			EditOp::Delete { object, snapshot } => {
				if !host.has_blockout() {
					return;
				}

				if let Some(restored) = restore_block(host, snapshot) {
					*object = restored;
				}
			}
		}
	}
}

pub fn light_snapshot(host: &dyn EditorHost, light: LightId) -> Option<SpotLight> {
	let mut snapshot = host.spot_light(light)?;

	snapshot.direction = snapshot.direction.normalize();

	Some(snapshot)
}

struct Entry {
	op: EditOp,
	group: i32,
}

pub struct EditHistory {
	entries: Vec<Entry>,
	applied: usize,
	capacity: usize,
}

impl Default for EditHistory {
	fn default() -> Self {
		Self::new()
	}
}

impl EditHistory {
	pub fn new() -> Self {
		Self {
			entries: Vec::new(),
			applied: 0,
			capacity: MAX_OPERATIONS,
		}
	}

	pub fn len(&self) -> usize {
		self.entries.len()
	}

	pub fn is_empty(&self) -> bool {
		self.entries.is_empty()
	}

	pub fn clear(&mut self) {
		self.entries.clear();
		self.applied = 0;
	}

	pub fn push(
		&mut self,
		host: &mut dyn EditorHost,
		selection: &mut Selection,
		op: EditOp,
		group: i32,
	) -> OpResult {
		self.entries.truncate(self.applied);

		while self.capacity > 0 && self.entries.len() >= self.capacity {
			let evict = self.entries[0].group.max(1) as usize;

			self.entries.drain(..evict.min(self.entries.len()));
		}

		self.entries.push(Entry { op, group });
		self.applied = self.entries.len();

		let last = self.entries.len() - 1;

		self.entries[last].op.execute(host, selection)
	}

	pub fn undo(&mut self, host: &mut dyn EditorHost, selection: &mut Selection) -> bool {
		if self.applied == 0 {
			return false;
		}

		let group = self.entries[self.applied - 1].group.max(1);
		let mut selection_changed = false;

		for _ in 0..group {
			if self.applied == 0 {
				break;
			}

			self.applied -= 1;

			let entry = &mut self.entries[self.applied];

			let old_object = match &entry.op {
				EditOp::Delete { object, .. } => Some(*object),
				_ => None,
			};

			let source = match &entry.op {
				EditOp::Dupe { source, .. } => Some(*source),
				_ => None,
			};

			entry.op.undo(host, selection);

			if let (Some(old), EditOp::Delete { object: new, .. }) = (old_object, &entry.op)
				&& old != *new
			{
				let new = *new;

				for other in &mut self.entries {
					other.op.remap(old, new);
				}
			}

			let entry = &mut self.entries[self.applied];

			match &entry.op {
				EditOp::Dupe { .. } => {
					if let Some(original) = source.and_then(|id| alive(host, id)) {
						selection.add(host, original);
					}
				}
				EditOp::Delete { object, .. } => {
					if host.object_exists(*object) {
						selection.add(host, *object);
					}
				}
				_ => {}
			}

			selection_changed |= entry.op.changes_objects();
		}

		selection_changed
	}

	pub fn redo(&mut self, host: &mut dyn EditorHost, selection: &mut Selection) -> bool {
		let mut remaining = 0;
		let mut started = false;
		let mut selection_changed = false;

		while self.applied < self.entries.len() {
			let entry = &mut self.entries[self.applied];

			self.applied += 1;

			if !started {
				remaining = entry.group.max(1);
				started = true;
			}

			let result = entry.op.execute(host, selection);

			if let EditOp::Dupe { source, .. } = &entry.op
				&& let Some(original) = alive(host, *source)
			{
				selection.remove(host, original);
			}

			let creates = matches!(
				entry.op,
				EditOp::Dupe { .. } | EditOp::Create { .. } | EditOp::CreateBrush { .. }
			);

			if creates && let Some(created) = result.object() {
				selection.add(host, created);
			}

			selection_changed |= entry.op.changes_objects();

			remaining -= 1;

			if remaining <= 0 {
				break;
			}
		}

		selection_changed
	}
}
