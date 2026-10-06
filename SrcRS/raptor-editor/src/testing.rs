use std::collections::{BTreeMap, HashSet};

use raptor_brush::{Brush, Plane};
use raptor_math::mat4::Quat;
use raptor_math::{Aabb, Mat4f, Vec3f};

use crate::host::*;
use crate::key::Key;

#[derive(Clone)]
pub struct MockObject {
	pub name: String,
	pub position: Vec3f,
	pub euler: Vec3f,
	pub bounds: Aabb,
	pub tags: u32,
	pub flags: u32,
	pub material: MaterialId,
	pub planes: Option<Vec<Plane>>,
	pub children: Vec<ObjectId>,
	pub dynamic: bool,
}

pub struct MockHost {
	pub objects: BTreeMap<u32, MockObject>,
	next_object: u32,
	pub lights: BTreeMap<u32, SpotLight>,
	next_light: u32,
	pub down: HashSet<Key>,
	pub pressed: HashSet<Key>,
	pub camera: Vec3f,
	pub forward: Vec3f,
	pub player: Vec3f,
	pub ray: Option<RayHit>,
	pub speed: f32,
	pub cvars: BTreeMap<String, i64>,
	pub marker: Option<Vec3f>,
	pub preview: Option<Vec<Plane>>,
	pub rebuilt_reflections: u32,
}

impl Default for MockHost {
	fn default() -> Self {
		Self {
			objects: BTreeMap::new(),
			next_object: 0,
			lights: BTreeMap::new(),
			next_light: 0,
			down: HashSet::new(),
			pressed: HashSet::new(),
			camera: Vec3f::ZERO,
			forward: Vec3f::FORWARD,
			player: Vec3f::ZERO,
			ray: None,
			speed: 1.0,
			cvars: BTreeMap::new(),
			marker: None,
			preview: None,
			rebuilt_reflections: 0,
		}
	}
}

impl MockHost {
	pub fn add_block(&mut self, position: Vec3f, half: f32) -> ObjectId {
		let id = self.next_object;
		self.next_object += 1;

		let brush = Brush::from_box(Vec3f::splat(-half), Vec3f::splat(half));

		self.objects.insert(
			id,
			MockObject {
				name: format!("Block_{id}"),
				position,
				euler: Vec3f::ZERO,
				bounds: Aabb::new(Vec3f::splat(-half), Vec3f::splat(half)),
				tags: TAG_BLOCKOUT,
				flags: 0,
				material: MaterialId(1),
				planes: Some(brush.planes),
				children: Vec::new(),
				dynamic: false,
			},
		);

		ObjectId(id)
	}

	pub fn press(&mut self, key: Key) {
		self.down.insert(key);
		self.pressed.insert(key);
	}

	pub fn end_frame(&mut self) {
		self.pressed.clear();
	}

	fn object(&self, id: ObjectId) -> &MockObject {
		&self.objects[&id.0]
	}

	fn object_mut(&mut self, id: ObjectId) -> &mut MockObject {
		self.objects.get_mut(&id.0).expect("object")
	}
}

impl InputHost for MockHost {
	fn key_down(&self, key: Key) -> bool {
		self.down.contains(&key)
	}

	fn key_pressed(&self, key: Key) -> bool {
		self.pressed.contains(&key)
	}
}

impl PlayerHost for MockHost {
	fn camera_position(&self) -> Vec3f {
		self.camera
	}

	fn camera_forward(&self) -> Vec3f {
		self.forward
	}

	fn player_position(&self) -> Vec3f {
		self.player
	}

	fn teleport_player(&mut self, position: Vec3f) {
		self.player = position;
	}

	fn set_player_speed_multiplier(&mut self, multiplier: f32) {
		self.speed = multiplier;
	}
}

impl CvarHost for MockHost {
	fn cvar_int(&self, name: &str, default: i64) -> i64 {
		self.cvars.get(name).copied().unwrap_or(default)
	}

	fn cvar_float(&self, name: &str, default: f32) -> f32 {
		self.cvars.get(name).map_or(default, |value| *value as f32)
	}

	fn set_cvar_int(&mut self, name: &str, value: i64) {
		self.cvars.insert(name.to_owned(), value);
	}

	fn set_cvar_float(&mut self, name: &str, value: f32) {
		self.cvars.insert(name.to_owned(), value as i64);
	}

	fn cvars(&self) -> Vec<CvarInfo> {
		Vec::new()
	}

	fn set_cvar_from_str(&mut self, _name: &str, _value: &str) -> bool {
		true
	}
}

impl PhysicsHost for MockHost {
	fn raycast(&self, _origin: Vec3f, _delta: Vec3f) -> Option<RayHit> {
		self.ray
	}

	fn raycast_objects(&self, _origin: Vec3f, _delta: Vec3f) -> Vec<ObjectId> {
		Vec::new()
	}

	fn body_is_dynamic(&self, _body: BodyId) -> bool {
		true
	}

	fn body_point_to_local(&self, _body: BodyId, point: Vec3f) -> Vec3f {
		point
	}

	fn body_hold(
		&mut self,
		_: BodyId,
		_: Vec3f,
		target: Vec3f,
		_: f32,
		_: f32,
		_: f32,
	) -> Option<Vec3f> {
		Some(target)
	}
}

impl ObjectHost for MockHost {
	fn object_exists(&self, id: ObjectId) -> bool {
		self.objects.contains_key(&id.0)
	}

	fn object_ids(&self) -> Vec<ObjectId> {
		self.objects.keys().map(|id| ObjectId(*id)).collect()
	}

	fn object_name(&self, id: ObjectId) -> String {
		self.object(id).name.clone()
	}

	fn object_position(&self, id: ObjectId) -> Vec3f {
		self.object(id).position
	}

	fn set_object_position(&mut self, id: ObjectId, position: Vec3f) {
		self.object_mut(id).position = position;
	}

	fn object_rotation(&self, _id: ObjectId) -> Quat {
		Quat::IDENTITY
	}

	fn object_euler(&self, id: ObjectId) -> Vec3f {
		self.object(id).euler
	}

	fn set_object_euler(&mut self, id: ObjectId, euler: Vec3f) {
		self.object_mut(id).euler = euler;
	}

	fn object_bounds(&self, id: ObjectId) -> Aabb {
		self.object(id).bounds
	}

	fn set_object_bounds(&mut self, id: ObjectId, bounds: Aabb) {
		self.object_mut(id).bounds = bounds;
	}

	fn object_tags(&self, id: ObjectId) -> u32 {
		self.object(id).tags
	}

	fn object_flags(&self, id: ObjectId) -> u32 {
		self.object(id).flags
	}

	fn set_object_tag(&mut self, id: ObjectId, bit: u32, enabled: bool) {
		let object = self.object_mut(id);

		if enabled {
			object.tags |= bit;
		} else {
			object.tags &= !bit;
		}
	}

	fn set_object_flag(&mut self, id: ObjectId, bit: u32, enabled: bool) {
		let object = self.object_mut(id);

		if enabled {
			object.flags |= bit;
		} else {
			object.flags &= !bit;
		}
	}

	fn object_children(&self, id: ObjectId) -> Vec<ObjectId> {
		self.object(id).children.clone()
	}

	fn object_parent(&self, _id: ObjectId) -> Option<ObjectId> {
		None
	}

	fn object_has_mesh(&self, _id: ObjectId) -> bool {
		true
	}

	fn object_is_player_layer(&self, _id: ObjectId) -> bool {
		false
	}

	fn object_in_world(&self, _id: ObjectId) -> bool {
		true
	}

	fn object_world_matrix(&self, id: ObjectId) -> Mat4f {
		Mat4f::as_translation(self.object(id).position)
	}

	fn object_raycast_bounds(
		&self,
		_id: ObjectId,
		_origin: Vec3f,
		_direction: Vec3f,
	) -> Option<(f32, Vec3f)> {
		None
	}

	fn object_contains_point(&self, _id: ObjectId, _point: Vec3f) -> bool {
		false
	}

	fn object_material(&self, id: ObjectId) -> MaterialId {
		self.object(id).material
	}

	fn set_object_material(&mut self, id: ObjectId, material: MaterialId) {
		self.object_mut(id).material = material;
	}

	fn object_has_dynamic_body(&self, id: ObjectId) -> bool {
		self.object(id).dynamic
	}

	fn raycast_probe_volumes(&self, _: Vec3f, _: Vec3f, _: f32) -> Option<(ObjectId, f32)> {
		None
	}
}

impl BlockoutHost for MockHost {
	fn has_blockout(&self) -> bool {
		true
	}

	fn brush_planes(&self, id: ObjectId) -> Option<Vec<Plane>> {
		self.objects.get(&id.0)?.planes.clone()
	}

	fn set_brush_planes(&mut self, id: ObjectId, planes: &[Plane]) {
		self.object_mut(id).planes = Some(planes.to_vec());
	}

	fn rebuild_block(&mut self, _id: ObjectId) {}

	fn new_block(&mut self, position: Vec3f) -> Option<ObjectId> {
		Some(self.add_block(position, 0.5))
	}

	fn dupe_block(&mut self, id: ObjectId) -> Option<ObjectId> {
		let source = self.object(id).clone();
		let dupe = self.add_block(source.position, 0.5);

		*self.object_mut(dupe) = source;

		Some(dupe)
	}

	fn restore_block(&mut self, snapshot: &BlockSnapshot, planes: &[Plane]) -> Option<ObjectId> {
		let id = self.add_block(snapshot.position, 0.5);

		let object = self.object_mut(id);
		object.planes = Some(planes.to_vec());
		object.material = snapshot.material;
		object.name = snapshot.name.clone();

		Some(id)
	}

	fn destroy_block(&mut self, id: ObjectId) {
		self.objects.remove(&id.0);
	}

	fn block_is_dynamic(&self, id: ObjectId) -> bool {
		self.object(id).dynamic
	}

	fn default_material(&self) -> MaterialId {
		MaterialId(1)
	}

	fn selection_material(&self) -> MaterialId {
		MaterialId(99)
	}

	fn materials(&self) -> Vec<(MaterialId, String)> {
		vec![
			(MaterialId(0), "Gray".to_owned()),
			(MaterialId(1), "Orange".to_owned()),
		]
	}

	fn show_preview(&mut self, _position: Vec3f, _rotation: Quat, planes: &[Plane]) {
		self.preview = Some(planes.to_vec());
	}

	fn hide_preview(&mut self) {
		self.preview = None;
	}

	fn set_transform_marker(&mut self, position: Option<Vec3f>) {
		self.marker = position;
	}

	fn blockout_path(&self) -> Option<String> {
		None
	}

	fn set_blockout_path(&mut self, _path: &str) {}

	fn save_blockout(&mut self, _path: &str) -> bool {
		true
	}

	fn load_blockout(&mut self, _path: &str) -> bool {
		true
	}
}

impl LightHost for MockHost {
	fn spot_lights(&self) -> Vec<LightId> {
		self.lights.keys().map(|id| LightId(*id)).collect()
	}

	fn spot_light(&self, id: LightId) -> Option<SpotLight> {
		self.lights.get(&id.0).cloned()
	}

	fn set_light_position(&mut self, id: LightId, position: Vec3f) {
		if let Some(light) = self.lights.get_mut(&id.0) {
			light.position = position;
		}
	}

	fn set_light_direction(&mut self, id: LightId, direction: Vec3f) {
		if let Some(light) = self.lights.get_mut(&id.0) {
			light.direction = direction;
		}
	}

	fn set_light_radius(&mut self, id: LightId, radius: f32) {
		if let Some(light) = self.lights.get_mut(&id.0) {
			light.radius = radius;
		}
	}

	fn set_light_cone(&mut self, id: LightId, inner: f32, outer: f32) {
		if let Some(light) = self.lights.get_mut(&id.0) {
			light.inner_angle = inner;
			light.outer_angle = outer;
		}
	}

	fn set_light_color(&mut self, id: LightId, color: Rgba) {
		if let Some(light) = self.lights.get_mut(&id.0) {
			light.color = color;
		}
	}

	fn set_light_intensity(&mut self, id: LightId, intensity: f32) {
		if let Some(light) = self.lights.get_mut(&id.0) {
			light.intensity = intensity;
		}
	}

	fn light_lumens(&self, id: LightId) -> f32 {
		self.lights.get(&id.0).map_or(0.0, |light| light.intensity)
	}

	fn set_light_lumens(&mut self, id: LightId, lumens: f32) {
		self.set_light_intensity(id, lumens);
	}

	fn create_spot_light(&mut self, light: &SpotLight) -> Option<LightId> {
		let id = self.next_light;
		self.next_light += 1;
		self.lights.insert(id, light.clone());

		Some(LightId(id))
	}

	fn destroy_light(&mut self, id: LightId) {
		self.lights.remove(&id.0);
	}

	fn light_count(&self) -> u32 {
		self.lights.len() as u32
	}

	fn light_name_in_use(&self, name: &str) -> bool {
		self.lights.values().any(|light| light.name == name)
	}
}

impl ProbeHost for MockHost {
	fn rebuild_reflection_probes(&mut self) {
		self.rebuilt_reflections += 1;
	}

	fn probe_status(&self) -> ProbeStatus {
		ProbeStatus::default()
	}

	fn bake_reflections(&mut self) {}

	fn bake_irradiance(&mut self) {}

	fn rebuild_probe_volumes(&mut self) -> u32 {
		0
	}

	fn probe_volume_count(&self) -> u32 {
		0
	}

	fn probe_count(&self) -> u32 {
		0
	}

	fn is_baking(&self) -> bool {
		false
	}

	fn save_probes(&mut self) -> bool {
		true
	}

	fn load_probes(&mut self) {}
}

impl DrawHost for MockHost {
	fn debug_line(&mut self, _: Vec3f, _: Vec3f, _: Rgba) {}

	fn debug_solid_box(&mut self, _: Vec3f, _: Vec3f, _: Rgba) {}

	fn debug_wire_box(&mut self, _: &Mat4f, _: Rgba) {}
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::editor::Editor;
	use crate::tools::EditorTool;
	use crate::ui_model::UiAction;

	fn frame(editor: &mut Editor, host: &mut MockHost) {
		editor.update(host, 1.0 / 60.0);
		host.end_frame();
	}

	fn selected(editor: &mut Editor, host: &mut MockHost, id: ObjectId) {
		editor.select_object(host, id, false);
	}

	#[test]
	fn selecting_swaps_the_material_and_deselecting_restores_it() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();
		let block = host.add_block(Vec3f::ZERO, 1.0);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Translate));
		editor.update(&mut host, 0.0);
		selected(&mut editor, &mut host, block);

		assert_eq!(host.object_material(block), MaterialId(99));

		editor.clear_selection(&mut host);

		assert_eq!(host.object_material(block), MaterialId(1));
	}

	#[test]
	fn dragging_with_translate_records_one_undoable_move() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();
		let block = host.add_block(Vec3f::ZERO, 1.0);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Translate));
		selected(&mut editor, &mut host, block);

		host.press(Key::MouseLeft);
		frame(&mut editor, &mut host);

		host.player = Vec3f::new(1.0, 0.0, 2.0);
		frame(&mut editor, &mut host);

		assert!(
			host.object_position(block)
				.is_close_to(&Vec3f::new(1.0, 0.0, 2.0), 1e-4)
		);

		host.down.remove(&Key::MouseLeft);
		frame(&mut editor, &mut host);
		frame(&mut editor, &mut host);

		assert_eq!(host.speed, 1.0);

		editor.undo(&mut host);

		assert!(host.object_position(block).is_close_to(&Vec3f::ZERO, 1e-4));

		editor.redo(&mut host);

		assert!(
			host.object_position(block)
				.is_close_to(&Vec3f::new(1.0, 0.0, 2.0), 1e-4)
		);
	}

	#[test]
	fn a_click_without_movement_leaves_no_history() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();
		let block = host.add_block(Vec3f::ZERO, 1.0);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Translate));
		selected(&mut editor, &mut host, block);

		host.press(Key::MouseLeft);
		frame(&mut editor, &mut host);
		host.down.remove(&Key::MouseLeft);
		frame(&mut editor, &mut host);
		frame(&mut editor, &mut host);

		assert!(editor.core().history.is_empty());
	}

	#[test]
	fn create_delete_and_dupe_round_trip_through_undo() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Translate));

		host.press(Key::K);
		frame(&mut editor, &mut host);

		assert_eq!(host.objects.len(), 1);
		assert_eq!(editor.selection_count(), 1);

		host.press(Key::Lctrl);
		host.press(Key::D);
		frame(&mut editor, &mut host);
		host.down.clear();

		assert_eq!(host.objects.len(), 2);

		host.press(Key::Backspace);
		frame(&mut editor, &mut host);

		assert_eq!(host.objects.len(), 1);

		editor.undo(&mut host);
		assert_eq!(host.objects.len(), 2);

		editor.undo(&mut host);
		assert_eq!(host.objects.len(), 1);

		editor.undo(&mut host);
		assert_eq!(host.objects.len(), 0);

		editor.redo(&mut host);
		assert_eq!(host.objects.len(), 1);
	}

	#[test]
	fn face_tool_pushes_a_scale_that_undo_reverts() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();
		let block = host.add_block(Vec3f::ZERO, 1.0);

		host.camera = Vec3f::new(0.0, 0.0, -5.0);
		host.forward = Vec3f::FORWARD;
		host.player = Vec3f::new(0.0, 0.0, -5.0);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Face));
		selected(&mut editor, &mut host, block);

		host.press(Key::MouseLeft);
		frame(&mut editor, &mut host);

		host.player = Vec3f::new(0.0, 0.0, -4.0);
		frame(&mut editor, &mut host);

		assert!(host.marker.is_some());

		host.down.remove(&Key::MouseLeft);
		frame(&mut editor, &mut host);
		frame(&mut editor, &mut host);

		let before = Brush::from_box(Vec3f::splat(-1.0), Vec3f::splat(1.0));
		let after = Brush::from_planes(&host.brush_planes(block).unwrap());

		assert!(after.is_valid());
		assert_ne!(after.bounds_min.z.to_bits(), before.bounds_min.z.to_bits());

		editor.undo(&mut host);

		let restored = Brush::from_planes(&host.brush_planes(block).unwrap());

		assert!(restored.bounds_min.is_close_to(&before.bounds_min, 1e-4));
		assert!(restored.bounds_max.is_close_to(&before.bounds_max, 1e-4));
	}

	#[test]
	fn undoing_a_group_reverts_every_member() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();
		let a = host.add_block(Vec3f::ZERO, 1.0);
		let b = host.add_block(Vec3f::new(5.0, 0.0, 0.0), 1.0);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Translate));
		editor.select_object(&mut host, a, false);
		editor.select_object(&mut host, b, true);

		host.press(Key::MouseLeft);
		frame(&mut editor, &mut host);
		host.player = Vec3f::new(2.0, 0.0, 0.0);
		frame(&mut editor, &mut host);
		host.down.remove(&Key::MouseLeft);
		frame(&mut editor, &mut host);
		frame(&mut editor, &mut host);

		editor.undo(&mut host);

		assert!(host.object_position(a).is_close_to(&Vec3f::ZERO, 1e-4));
		assert!(
			host.object_position(b)
				.is_close_to(&Vec3f::new(5.0, 0.0, 0.0), 1e-4)
		);
	}

	#[test]
	fn object_bits_toggle_with_undo_and_probe_rebuild() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();
		let block = host.add_block(Vec3f::ZERO, 1.0);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Translate));
		selected(&mut editor, &mut host, block);

		editor.apply_action(
			&mut host,
			UiAction::SetObjectBit {
				is_tag: true,
				bit: TAG_REFLECTION_PROBE,
				enabled: true,
			},
		);

		assert!(host.object_tags(block) & TAG_REFLECTION_PROBE != 0);
		assert_eq!(host.rebuilt_reflections, 1);

		editor.undo(&mut host);

		assert!(host.object_tags(block) & TAG_REFLECTION_PROBE == 0);
		assert_eq!(host.rebuilt_reflections, 2);
	}

	#[test]
	fn light_tool_creates_and_deletes_with_undo() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();

		host.ray = Some(RayHit {
			point: Vec3f::new(1.0, 2.0, 3.0),
			normal: Vec3f::UP,
			body: None,
		});

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Light));

		host.press(Key::K);
		frame(&mut editor, &mut host);

		assert_eq!(host.lights.len(), 1);

		let snapshot = editor.snapshot(&mut host);
		assert!(snapshot.light.is_some());

		editor.undo(&mut host);
		assert_eq!(host.lights.len(), 0);

		editor.redo(&mut host);
		assert_eq!(host.lights.len(), 1);
	}

	#[test]
	fn tool_switching_drops_models_for_tools_that_do_not_use_them() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();
		let model = host.add_block(Vec3f::ZERO, 1.0);
		host.object_mut(model).tags = 0;

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Translate));
		selected(&mut editor, &mut host, model);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Face));

		assert_eq!(editor.selection_count(), 0);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::None));
		assert!(editor.is_simulation_mode());
	}

	#[test]
	fn the_history_drops_the_oldest_group_when_full() {
		let mut host = MockHost::default();
		let mut editor = Editor::new();
		let block = host.add_block(Vec3f::ZERO, 1.0);

		editor.apply_action(&mut host, UiAction::SetTool(EditorTool::Translate));

		for i in 0..300 {
			let before = host.object_position(block);

			editor.core_mut().push(
				&mut host,
				crate::history::EditOp::Move {
					object: block,
					before,
					after: Vec3f::new(i as f32, 0.0, 0.0),
				},
				1,
			);
		}

		assert_eq!(editor.core().history.len(), 256);
	}
}
