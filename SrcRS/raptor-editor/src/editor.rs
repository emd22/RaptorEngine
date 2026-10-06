use std::sync::mpsc::{self, Receiver, Sender};

use raptor_brush::edit::world_box;
use raptor_math::mat4::Quat;
use raptor_math::{Mat4f, Vec3f};

use crate::core::EditorCore;
use crate::history::{
	EDITABLE_FLAGS, EDITABLE_TAGS, EditOp, ObjectSnapshot, can_edit_flag, can_edit_tag,
	capture_object_state, snapshot_block,
};
use crate::host::*;
use crate::key::Key;
use crate::tools::*;
use crate::ui_model::*;

const PICK_RANGE: f32 = 4.0;
const CREATE_RANGE: f32 = 20.0;
const DRAG_SPEED_MULTIPLIER: f32 = 0.5;
const SELECTED_COLOR: Rgba = [255, 220, 60, 255];
const HEADBOB_CVAR: &str = "b_headbob_enabled";
const SHOW_VOLUMES_CVAR: &str = "r_show_volumes";

const DEFAULT_APERTURE: f32 = 16.0;
const DEFAULT_SHUTTER_TIME: f32 = 0.01;
const DEFAULT_ISO: f32 = 100.0;
const MIN_EXPOSURE_VALUE: f32 = 1e-4;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Notice {
	Error(String),
	Info(String),
}

#[derive(Default)]
struct Tools {
	translate: TranslateTool,
	face: FaceTool,
	rotate: RotateTool,
	create: CreateTool,
	clip: ClipTool,
	light: LightTool,
	bounds: BoundsTool,
	grab: GrabTool,
}

impl Tools {
	fn get(&mut self, tool: EditorTool) -> Option<&mut dyn Tool> {
		Some(match tool {
			EditorTool::None => return None,
			EditorTool::Translate => &mut self.translate,
			EditorTool::Face => &mut self.face,
			EditorTool::Rotate => &mut self.rotate,
			EditorTool::Create => &mut self.create,
			EditorTool::Clip => &mut self.clip,
			EditorTool::Light => &mut self.light,
			EditorTool::Bounds => &mut self.bounds,
			EditorTool::Grab => &mut self.grab,
		})
	}
}

pub struct Editor {
	core: EditorCore,
	tools: Tools,
	current: EditorTool,
	dragging: bool,
	had_headbob: bool,
	sender: Sender<UiAction>,
	receiver: Receiver<UiAction>,
	reloads: Vec<ReloadTarget>,
	notices: Vec<Notice>,
}

impl Default for Editor {
	fn default() -> Self {
		Self::new()
	}
}

fn is_blockout(host: &dyn ObjectHost, object: ObjectId) -> bool {
	host.object_tags(object) & TAG_BLOCKOUT != 0
}

impl Editor {
	pub fn new() -> Self {
		let (sender, receiver) = mpsc::channel();

		Self {
			core: EditorCore::default(),
			tools: Tools::default(),
			current: EditorTool::None,
			dragging: false,
			had_headbob: false,
			sender,
			receiver,
			reloads: Vec::new(),
			notices: Vec::new(),
		}
	}

	pub fn sender(&self) -> Sender<UiAction> {
		self.sender.clone()
	}

	pub fn core(&self) -> &EditorCore {
		&self.core
	}

	pub fn core_mut(&mut self) -> &mut EditorCore {
		&mut self.core
	}

	pub fn current_tool(&self) -> EditorTool {
		self.current
	}

	pub fn is_simulation_mode(&self) -> bool {
		self.current == EditorTool::None
	}

	pub fn take_reload_requests(&mut self) -> Vec<ReloadTarget> {
		std::mem::take(&mut self.reloads)
	}

	pub fn take_notices(&mut self) -> Vec<Notice> {
		std::mem::take(&mut self.notices)
	}

	pub fn selection_count(&self) -> usize {
		self.core.selection.len()
	}

	pub fn snap_step(&self) -> f32 {
		self.core.snap_step()
	}

	pub fn snap_to_grid(&self, position: Vec3f) -> Vec3f {
		self.core.snap_to_grid(position)
	}

	pub fn angle_snap_step(&self) -> f32 {
		self.core.angle_snap_step()
	}

	fn with_tool<R>(
		&mut self,
		host: &mut dyn EditorHost,
		tool: EditorTool,
		f: impl FnOnce(&mut dyn Tool, &mut ToolCtx) -> R,
	) -> Option<R> {
		let tool = self.tools.get(tool)?;

		Some(f(
			tool,
			&mut ToolCtx {
				host,
				core: &mut self.core,
			},
		))
	}

	pub fn update(&mut self, host: &mut dyn EditorHost, delta_time: f32) {
		self.process_ui(host);

		if self.is_simulation_mode() {
			return;
		}

		self.core.selection.prune(host);

		self.handle_hotkeys(host);

		let flags = self.current.flags();

		if flags.uses_selection && host.key_pressed(Key::MouseLeft) {
			self.pick_object(host);
		}

		let mouse_down = host.key_down(Key::MouseLeft);

		if self.dragging && !mouse_down {
			self.end_drag(host);
		}

		if self.dragging && flags.uses_selection && self.core.selection.is_empty() {
			self.cancel_drag(host);
		}

		if !self.dragging {
			let current = self.current;
			self.with_tool(host, current, |tool, ctx| tool.controls(ctx));

			let has_target = !flags.uses_selection || !self.core.selection.is_empty();

			if mouse_down && has_target {
				self.begin_drag(host);
			}
		}

		if self.dragging {
			let current = self.current;
			self.with_tool(host, current, |tool, ctx| tool.update(ctx, delta_time));
		}

		self.draw_model_selection(host);
	}

	fn begin_drag(&mut self, host: &mut dyn EditorHost) {
		self.set_movement_damped(host, true);
		self.dragging = true;

		let current = self.current;
		self.with_tool(host, current, |tool, ctx| tool.begin(ctx));
	}

	fn end_drag(&mut self, host: &mut dyn EditorHost) {
		if !self.dragging {
			return;
		}

		self.dragging = false;

		let current = self.current;
		self.with_tool(host, current, |tool, ctx| tool.finalize(ctx));

		self.set_movement_damped(host, false);
	}

	fn cancel_drag(&mut self, host: &mut dyn EditorHost) {
		if !self.dragging {
			return;
		}

		self.dragging = false;

		let current = self.current;
		self.with_tool(host, current, |tool, ctx| tool.cancel(ctx));

		self.hide_tool_markers(host);
		self.set_movement_damped(host, false);
	}

	fn set_movement_damped(&mut self, host: &mut dyn EditorHost, damped: bool) {
		if damped {
			self.had_headbob = host.cvar_bool(HEADBOB_CVAR, false);

			host.set_player_speed_multiplier(DRAG_SPEED_MULTIPLIER);
			host.set_cvar_bool(HEADBOB_CVAR, false);
		} else {
			host.set_player_speed_multiplier(1.0);
			host.set_cvar_bool(HEADBOB_CVAR, self.had_headbob);
		}
	}

	fn hide_tool_markers(&mut self, host: &mut dyn EditorHost) {
		if host.has_blockout() {
			host.hide_preview();
			host.set_transform_marker(None);
		}
	}

	pub fn set_tool(&mut self, host: &mut dyn EditorHost, tool: EditorTool) {
		if tool == self.current {
			return;
		}

		self.end_drag(host);

		let previous = self.current;
		self.with_tool(host, previous, |tool, ctx| tool.leave(ctx));
		self.hide_tool_markers(host);

		self.current = tool;

		let flags = tool.flags();

		if tool == EditorTool::None || flags.clears_selection {
			self.core.selection.clear(host);
		} else if !flags.uses_models {
			self.core.deselect_models(host);
		}

		self.with_tool(host, tool, |tool, ctx| tool.enter(ctx));
	}

	fn handle_hotkeys(&mut self, host: &mut dyn EditorHost) {
		if host.key_pressed(Key::U) {
			self.core.snap_enabled = !self.core.snap_enabled;
		}

		if host.key_pressed(Key::Minus) {
			self.core.adjust_snap_level(-1);
		}
		if host.key_pressed(Key::Equals) {
			self.core.adjust_snap_level(1);
		}

		if self.dragging {
			return;
		}

		if host.combo_pressed(Key::Lctrl, Key::Z) {
			if host.key_down(Key::Lshift) {
				self.redo(host);
			} else {
				self.undo(host);
			}
		}

		let tool_keys = [
			(Key::T, EditorTool::Translate),
			(Key::F, EditorTool::Face),
			(Key::B, EditorTool::Create),
			(Key::C, EditorTool::Clip),
			(Key::I, EditorTool::Light),
			(Key::O, EditorTool::Bounds),
			(Key::Y, EditorTool::Grab),
		];

		for (key, tool) in tool_keys {
			if host.key_pressed(key) {
				self.set_tool(host, tool);
			}
		}

		if host.key_pressed(Key::R) && !host.key_down(Key::Lshift) {
			self.set_tool(host, EditorTool::Rotate);
		}

		if host.key_pressed(Key::K) {
			if self.current == EditorTool::Light {
				self.create_light_at_crosshair(host);
			} else {
				self.create_object_at_crosshair(host);
			}
		}

		if self.core.selection.is_empty() {
			return;
		}

		if host.key_pressed(Key::Tab) {
			self.core.clear_selection(host);
		}

		if host.key_pressed(Key::Backspace) {
			self.delete_selection(host);
		}

		if host.combo_pressed(Key::Lctrl, Key::D) {
			self.dupe_selection(host);
		}
	}

	fn create_light_at_crosshair(&mut self, host: &mut dyn EditorHost) {
		let mut ctx = ToolCtx {
			host,
			core: &mut self.core,
		};

		self.tools.light.create_at_crosshair(&mut ctx);
	}

	fn pick_probe_volume(
		&self,
		host: &dyn EditorHost,
		origin: Vec3f,
		direction: Vec3f,
	) -> Option<ObjectId> {
		let (volume, distance) = host.raycast_probe_volumes(origin, direction, PICK_RANGE)?;

		if let Some(solid) = host.raycast(origin, direction * PICK_RANGE)
			&& (solid.point - origin).length() <= distance
		{
			return None;
		}

		Some(volume)
	}

	fn model_root(host: &dyn EditorHost, mut object: ObjectId) -> ObjectId {
		while let Some(parent) = host.object_parent(object) {
			if !host.object_exists(parent) {
				break;
			}

			object = parent;
		}

		object
	}

	fn pick_model(
		&self,
		host: &dyn EditorHost,
		origin: Vec3f,
		direction: Vec3f,
	) -> Option<(ObjectId, f32)> {
		let mut nearest = None;
		let mut nearest_distance = PICK_RANGE;

		for object in host.object_ids() {
			if is_blockout(host, object)
				|| !host.object_has_mesh(object)
				|| host.object_is_player_layer(object)
			{
				continue;
			}

			let Some((distance, _)) = host.object_raycast_bounds(object, origin, direction) else {
				continue;
			};

			if distance < 0.0 || distance >= nearest_distance {
				continue;
			}

			if host.object_contains_point(object, origin)
				|| !host.object_in_world(Self::model_root(host, object))
			{
				continue;
			}

			nearest = Some(object);
			nearest_distance = distance;
		}

		nearest.map(|object| (Self::model_root(host, object), nearest_distance))
	}

	fn pick_object(&mut self, host: &mut dyn EditorHost) {
		let append = host.key_down(Key::Lalt);

		if !self.core.selection.is_empty() && !append {
			return;
		}

		let origin = host.camera_position();
		let direction = host.camera_forward();

		if host.cvar_int(SHOW_VOLUMES_CVAR, 0) != 0
			&& let Some(volume) = self.pick_probe_volume(host, origin, direction)
			&& self.core.select_object(host, volume, append)
		{
			return;
		}

		if self.current.flags().uses_models
			&& let Some((model, distance)) = self.pick_model(host, origin, direction)
		{
			let solid_closer = host
				.raycast(origin, direction * PICK_RANGE)
				.is_some_and(|solid| distance > (solid.point - origin).length());

			if !solid_closer && self.core.select_object(host, model, append) {
				return;
			}
		}

		for object in host.raycast_objects(origin, direction * PICK_RANGE) {
			if self.core.select_object(host, object, append) {
				return;
			}
		}

		self.core.clear_selection(host);
	}

	fn draw_model_bounds(host: &mut dyn EditorHost, object: ObjectId) {
		if host.object_has_mesh(object) {
			let bounds = host.object_bounds(object);
			let half_extent = (bounds.max - bounds.min) * 0.5;
			let center = (bounds.max + bounds.min) * 0.5;

			let transform = Mat4f::as_scale(half_extent)
				* Mat4f::as_translation(center)
				* host.object_world_matrix(object);

			host.debug_wire_box(&transform, SELECTED_COLOR);
		}

		for child in host.object_children(object) {
			if host.object_exists(child) {
				Self::draw_model_bounds(host, child);
			}
		}
	}

	fn draw_model_selection(&mut self, host: &mut dyn EditorHost) {
		let objects: Vec<ObjectId> = self.core.selection.objects().collect();

		for object in objects {
			if !is_blockout(host, object) {
				Self::draw_model_bounds(host, object);
			}
		}
	}

	pub fn select_object(
		&mut self,
		host: &mut dyn EditorHost,
		object: ObjectId,
		append: bool,
	) -> bool {
		self.core.select_object(host, object, append)
	}

	pub fn clear_selection(&mut self, host: &mut dyn EditorHost) {
		self.core.clear_selection(host);
	}

	pub fn set_stored_material(
		&mut self,
		host: &mut dyn EditorHost,
		object: ObjectId,
		material: MaterialId,
	) {
		self.core.set_stored_material(host, object, material);
	}

	pub fn undo(&mut self, host: &mut dyn EditorHost) {
		self.core.undo(host);
	}

	pub fn redo(&mut self, host: &mut dyn EditorHost) {
		self.core.redo(host);
	}

	pub fn forget_objects(&mut self, host: &mut dyn EditorHost) {
		self.cancel_drag(host);
		self.core.forget_objects();
	}

	pub fn delete_object(&mut self, host: &mut dyn EditorHost, object: ObjectId, group: i32) {
		if !host.object_exists(object) {
			return;
		}

		let snapshot = snapshot_block(host, &self.core.selection, object);

		self.core
			.push(host, EditOp::Delete { object, snapshot }, group);
	}

	fn selected_blockouts(&self, host: &dyn EditorHost) -> Vec<ObjectId> {
		self.core
			.selection
			.objects()
			.filter(|object| is_blockout(host, *object))
			.collect()
	}

	fn delete_selection(&mut self, host: &mut dyn EditorHost) {
		let objects = self.selected_blockouts(host);
		let group = objects.len() as i32;

		for object in objects {
			self.delete_object(host, object, group);
		}
	}

	fn dupe_selection(&mut self, host: &mut dyn EditorHost) {
		let originals = self.selected_blockouts(host);

		if originals.is_empty() {
			return;
		}

		let group = originals.len() as i32;
		let mut dupes = Vec::new();

		for source in originals {
			let origin = host.object_position(source);

			let result = self.core.push(
				host,
				EditOp::Dupe {
					source,
					origin,
					dupe: None,
				},
				group,
			);

			if let Some(dupe) = result.object() {
				dupes.push(dupe);
			}
		}

		self.core.selection.clear(host);

		for dupe in dupes {
			self.core.selection.add(host, dupe);
		}
	}

	fn create_object_at_crosshair(&mut self, host: &mut dyn EditorHost) {
		let origin = host.camera_position();

		let point = host
			.raycast(origin, host.camera_forward() * CREATE_RANGE)
			.map_or(Vec3f::ZERO, |hit| hit.point);

		let position = self.core.snap_to_grid(point);

		let created = self.core.push(
			host,
			EditOp::Create {
				position,
				object: None,
			},
			1,
		);

		match created.object() {
			Some(object) => {
				self.core.select_object(host, object, false);
			}
			None => self.core.clear_selection(host),
		}
	}

	pub fn create_reflection_probe_at_player(
		&mut self,
		host: &mut dyn EditorHost,
	) -> Option<ObjectId> {
		let half_extent = Vec3f::new(2.0, 1.5, 2.0);

		if !host.has_blockout() {
			return None;
		}

		let center = self.core.snap_to_grid(host.camera_position());
		let (planes, position) = world_box(center - half_extent, center + half_extent);

		if !crate::blockout::is_valid_brush(&planes) {
			return None;
		}

		let snapshot = ObjectSnapshot {
			block: BlockSnapshot {
				name: String::new(),
				position,
				rotation: Quat::IDENTITY,
				material: host.default_material(),
				is_probe_volume: true,
				is_reflection_probe: true,
				is_dynamic: false,
			},
			planes,
		};

		let created = self
			.core
			.push(
				host,
				EditOp::CreateBrush {
					snapshot,
					object: None,
				},
				1,
			)
			.object()?;

		self.core.select_object(host, created, false);
		host.rebuild_reflection_probes();

		Some(created)
	}

	pub fn set_selection_reflection_probe(
		&mut self,
		host: &mut dyn EditorHost,
		enabled: bool,
	) -> u32 {
		let mut changed = 0;

		for object in self.core.selection.objects().collect::<Vec<_>>() {
			if (host.object_tags(object) & TAG_REFLECTION_PROBE != 0) == enabled {
				continue;
			}

			host.set_object_tag(object, TAG_REFLECTION_PROBE, enabled);
			changed += 1;
		}

		if changed > 0 {
			host.rebuild_reflection_probes();
		}

		changed
	}

	pub fn set_selection_object_bit(
		&mut self,
		host: &mut dyn EditorHost,
		is_tag: bool,
		bit: u32,
		enabled: bool,
	) -> u32 {
		let targets: Vec<ObjectId> = self
			.core
			.selection
			.objects()
			.filter(|object| {
				let can_edit = if is_tag {
					can_edit_tag(host, *object, bit)
				} else {
					can_edit_flag(host, *object, bit)
				};

				let current = if is_tag {
					host.object_tags(*object)
				} else {
					host.object_flags(*object)
				};

				can_edit && ((current & bit) != 0) != enabled
			})
			.collect();

		let group = targets.len() as i32;

		for object in &targets {
			let (tags_before, nodes_before) = capture_object_state(host, *object);

			self.core.push(
				host,
				EditOp::ObjectState {
					object: *object,
					bit,
					is_tag,
					enabled,
					tags_before,
					nodes_before,
				},
				group,
			);
		}

		group as u32
	}

	pub fn run_command(&mut self, host: &mut dyn EditorHost, name: &str) -> Option<Vec<String>> {
		let mut out = Vec::new();

		match name {
			"snap2w" => self.command_snap_to_world(host),
			"probevol" => {
				let changed =
					self.mark_selection(host, TAG_PROBE_VOLUME, true, "probe volumes", &mut out);
				let total = host.rebuild_probe_volumes();

				out.push(format!(
					"probe volumes: marked {changed} brush(es), {total} now defined, {} probes",
					host.probe_count()
				));
			}
			"probevolclear" => {
				let changed =
					self.mark_selection(host, TAG_PROBE_VOLUME, false, "probe volumes", &mut out);
				let total = host.rebuild_probe_volumes();

				out.push(format!(
					"probe volumes: unmarked {changed} brush(es), {total} still defined, {} probes",
					host.probe_count()
				));
			}
			"proberebuild" => {
				let total = host.rebuild_probe_volumes();

				out.push(format!(
					"probe volumes: {total} from brushes, {} volume(s), {} probes",
					host.probe_volume_count(),
					host.probe_count()
				));
			}
			"probebake" => {
				if host.is_baking() {
					out.push("probe bake already running".to_owned());
				} else {
					host.bake_irradiance();
				}
			}
			"probesave" => {
				if host.is_baking() {
					out.push("cannot save while the probes are baking".to_owned());
				} else if host.save_probes() {
					out.push("probes saved".to_owned());
				}
			}
			"reflprobe" => {
				let changed = self.mark_selection(
					host,
					TAG_REFLECTION_PROBE,
					true,
					"reflection probes",
					&mut out,
				);
				host.rebuild_reflection_probes();

				out.push(format!(
					"reflection probes: marked {changed} brush(es), {} probe(s) now defined, run reflbake",
					host.probe_status().reflection_probes
				));
			}
			"reflprobeclear" => {
				let changed = self.mark_selection(
					host,
					TAG_REFLECTION_PROBE,
					false,
					"reflection probes",
					&mut out,
				);
				host.rebuild_reflection_probes();

				out.push(format!(
					"reflection probes: unmarked {changed} brush(es), {} probe(s) still defined",
					host.probe_status().reflection_probes
				));
			}
			"reflbake" => {
				if host.is_baking() {
					out.push("probe bake already running".to_owned());
				} else {
					host.bake_reflections();
				}
			}
			"lock" => {
				for object in self.core.selection.objects().collect::<Vec<_>>() {
					host.set_object_tag(object, TAG_LOCK_TRANSFORM, true);
				}
			}
			_ => return None,
		}

		Some(out)
	}

	fn mark_selection(
		&mut self,
		host: &mut dyn EditorHost,
		tag: u32,
		enabled: bool,
		what: &str,
		out: &mut Vec<String>,
	) -> u32 {
		if self.core.selection.is_empty() {
			out.push(format!("{what}: nothing selected"));
			return 0;
		}

		let mut changed = 0;

		for object in self.core.selection.objects().collect::<Vec<_>>() {
			if (host.object_tags(object) & tag != 0) == enabled {
				continue;
			}

			host.set_object_tag(object, tag, enabled);
			changed += 1;
		}

		changed
	}

	fn command_snap_to_world(&mut self, host: &mut dyn EditorHost) {
		let moves: Vec<(ObjectId, Vec3f, Vec3f)> = self
			.core
			.selection
			.objects()
			.map(|object| {
				let position = host.object_position(object);

				(object, position, self.core.snap_to_grid(position))
			})
			.filter(|(_, position, snapped)| !(*position - *snapped).is_near_zero(1e-5))
			.collect();

		let group = moves.len() as i32;

		for (object, before, after) in moves {
			self.core.push(
				host,
				EditOp::Move {
					object,
					before,
					after,
				},
				group,
			);
		}
	}

	fn process_ui(&mut self, host: &mut dyn EditorHost) {
		while let Ok(action) = self.receiver.try_recv() {
			self.apply_action(host, action);
		}
	}

	pub fn apply_action(&mut self, host: &mut dyn EditorHost, action: UiAction) {
		match action {
			UiAction::SetTool(tool) => self.set_tool(host, tool),
			UiAction::Reload(target) => self.reloads.push(target),
			UiAction::OpenBlockout(path) => {
				if host.load_blockout(&path) {
					self.forget_objects(host);
					host.set_blockout_path(&path);
					host.load_probes();
				} else {
					self.notices.push(Notice::Error(
						"That file could not be loaded as a blockout.".to_owned(),
					));
				}
			}
			UiAction::SaveBlockout => {
				if let Some(path) = host.blockout_path() {
					host.save_blockout(&path);
				}
			}
			UiAction::SaveBlockoutAs(path) => {
				host.set_blockout_path(&path);
				host.save_blockout(&path);
			}
			UiAction::SelectObject(object) => {
				if host.object_exists(object) && !self.is_simulation_mode() {
					self.core.select_object(host, object, false);
				}
			}
			UiAction::SetMaterialSlot(slot) => {
				let Some(object) = self.core.selection.last() else {
					return;
				};

				if let Some((material, _)) = host.materials().get(slot).cloned() {
					self.core.set_stored_material(host, object, material);
				}
			}
			UiAction::SetObjectBit {
				is_tag,
				bit,
				enabled,
			} => {
				self.set_selection_object_bit(host, is_tag, bit, enabled);
			}
			UiAction::TeleportPlayer(position) => host.teleport_player(position),
			UiAction::SetCvarInt(name, value) => host.set_cvar_int(&name, value),
			UiAction::SetCvarFloat(name, value) => host.set_cvar_float(&name, value),
			UiAction::SetCvarBool(name, value) => host.set_cvar_bool(&name, value),
			UiAction::SetCvarFromStr(name, value) => {
				if !host.set_cvar_from_str(&name, &value) {
					self.notices
						.push(Notice::Error(format!("Invalid value for {name}")));
				}
			}
			UiAction::SetReflectionFallback(enabled) => {
				host.set_cvar_bool("r_reflection_level_probe", enabled);
				host.rebuild_reflection_probes();
			}
			UiAction::NewReflectionProbeAtPlayer => {
				self.create_reflection_probe_at_player(host);
			}
			UiAction::MarkSelectionReflectionProbe(enabled) => {
				self.set_selection_reflection_probe(host, enabled);
			}
			UiAction::BakeReflections => {
				host.rebuild_reflection_probes();
				host.bake_reflections();
			}
			UiAction::BakeAll => {
				host.rebuild_probe_volumes();
				host.bake_irradiance();
			}
			UiAction::SaveProbes => {
				host.save_probes();
			}
			UiAction::LightPosition(position) => {
				self.edit_light(host, |host, light| host.set_light_position(light, position))
			}
			UiAction::LightRadius(radius) => {
				self.edit_light(host, |host, light| host.set_light_radius(light, radius))
			}
			UiAction::LightIntensity(intensity) => {
				self.edit_light(host, |host, light| {
					host.set_light_intensity(light, intensity)
				});
			}
			UiAction::LightLumens(lumens) => {
				self.edit_light(host, |host, light| host.set_light_lumens(light, lumens))
			}
			UiAction::LightOuterAngle(degrees) => self.edit_light(host, |host, light| {
				if let Some(state) = host.spot_light(light) {
					host.set_light_cone(light, state.inner_angle, degrees.to_radians());
				}
			}),
			UiAction::LightInnerAngle(degrees) => self.edit_light(host, |host, light| {
				if let Some(state) = host.spot_light(light) {
					host.set_light_cone(light, degrees.to_radians(), state.outer_angle);
				}
			}),
			UiAction::LightColor(color) => {
				self.edit_light(host, |host, light| host.set_light_color(light, color))
			}
			UiAction::ViewportFocusLost => {}
		}
	}

	fn edit_light(
		&mut self,
		host: &mut dyn EditorHost,
		f: impl FnOnce(&mut dyn EditorHost, LightId),
	) {
		if self.current != EditorTool::Light {
			return;
		}

		let ctx = ToolCtx {
			host,
			core: &mut self.core,
		};

		if let Some(light) = self.tools.light.selected(&ctx) {
			f(ctx.host, light);
		}
	}

	pub fn snapshot(&mut self, host: &mut dyn EditorHost) -> PanelSnapshot {
		let materials = host.materials();

		let world = {
			let shutter = host.cvar_float("r_shutter", DEFAULT_SHUTTER_TIME);

			WorldPanel {
				debug_bounds_mask: host.cvar_int("r_debug_bounds", 0),
				reflection_view: host.cvar_int("r_reflection_debug", 0).clamp(0, 2),
				reflections_enabled: host.cvar_int("r_reflection_probes", 1) != 0,
				reflection_fallback: host.cvar_int("r_reflection_level_probe", 0) != 0,
				show_probe_volumes: host.cvar_int(SHOW_VOLUMES_CVAR, 0) != 0,
				probes: host.probe_status(),
				aperture: host.cvar_float("r_aperture", DEFAULT_APERTURE),
				shutter_denominator: 1.0 / shutter.max(MIN_EXPOSURE_VALUE),
				iso: host.cvar_float("r_iso", DEFAULT_ISO),
				compensation: host.cvar_float("r_exposure_ev", 0.0),
			}
		};

		let object = self.core.selection.last().map(|object| {
			let name = host.object_name(object);
			let name = if name.is_empty() {
				"(unnamed)".to_owned()
			} else {
				name
			};

			let mut label = format!("Selected '{name}'");

			if self.core.selection.len() > 1 {
				label.push_str(&format!(" (+{} more)", self.core.selection.len() - 1));
			}

			let stored = self.core.selection.stored_material(host, object);
			let material_slot = materials
				.iter()
				.position(|(id, _)| *id == stored)
				.map_or(-1, |slot| slot as i32);

			let editable = |bits: &[u32], check: &dyn Fn(u32) -> bool| {
				bits.iter()
					.filter(|bit| check(**bit))
					.fold(0, |mask, bit| mask | bit)
			};

			ObjectPanel {
				label,
				tags: host.object_tags(object),
				flags: host.object_flags(object),
				editable_tags: editable(&EDITABLE_TAGS, &|bit| can_edit_tag(host, object, bit)),
				editable_flags: editable(&EDITABLE_FLAGS, &|bit| can_edit_flag(host, object, bit)),
				material_slot,
				material_editable: is_blockout(host, object),
			}
		});

		let light = if self.current == EditorTool::Light {
			let ctx = ToolCtx {
				host,
				core: &mut self.core,
			};

			self.tools.light.selected(&ctx).and_then(|id| {
				host.spot_light(id).map(|state| LightPanel {
					id,
					name: state.name,
					color: state.color,
					position: state.position,
					radius: state.radius,
					intensity: state.intensity,
					lumens: host.light_lumens(id),
					outer_degrees: state.outer_angle.to_degrees(),
					inner_degrees: state.inner_angle.to_degrees(),
				})
			})
		} else {
			None
		};

		PanelSnapshot {
			tool: self.current,
			world,
			materials: materials.into_iter().map(|(_, name)| name).collect(),
			object,
			light,
			blockout_path: host.blockout_path(),
		}
	}

	pub fn object_rows(&self, host: &dyn EditorHost) -> Vec<ObjectRow> {
		host.object_ids()
			.into_iter()
			.map(|id| ObjectRow {
				id,
				name: host.object_name(id),
				tags: host.object_tags(id),
				position: host.object_position(id),
			})
			.collect()
	}
}
