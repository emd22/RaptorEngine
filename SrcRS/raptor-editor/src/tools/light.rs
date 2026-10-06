use raptor_math::Vec3f;

use super::{Tool, ToolCtx};
use crate::geom;
use crate::history::{EditOp, light_snapshot};
use crate::host::{LightId, Rgba, SpotLight};
use crate::key::Key;

const PICK_RANGE: f32 = 40.0;
const PICK_RADIUS: f32 = 0.3;
const AIM_FALLBACK_DISTANCE: f32 = 10.0;
const AIM_RANGE: f32 = 100.0;
const MARKER_HALF_EXTENT: f32 = 0.12;
const DIRECTION_LENGTH: f32 = 1.5;
const DIRECTION_TIP_HALF_EXTENT: f32 = 0.05;
const EDIT_TOLERANCE: f32 = 0.0005;
const HEIGHT_STEPS_PER_SECOND: f32 = 8.0;
const NEW_LIGHT_RADIUS: f32 = 8.0;
const MIN_AIM_DISTANCE: f32 = 0.05;

const LIGHT_COLOR: Rgba = [255, 170, 40, 255];
const SELECTED_LIGHT_COLOR: Rgba = [255, 255, 90, 255];

#[derive(Default)]
pub struct LightTool {
	selected: Option<LightId>,
	moving: bool,
	move_origin: Vec3f,
	player_origin: Vec3f,
	height_offset: f32,
	editing: bool,
	position_before: Vec3f,
	direction_before: Vec3f,
}

impl LightTool {
	pub fn selected(&mut self, ctx: &ToolCtx) -> Option<LightId> {
		let id = self.selected?;

		if ctx.host.spot_light(id).is_none() {
			self.deselect();
			return None;
		}

		Some(id)
	}

	pub fn selected_id(&self) -> Option<LightId> {
		self.selected
	}

	pub fn create_at_crosshair(&mut self, ctx: &mut ToolCtx) {
		let origin = ctx.host.camera_position();
		let forward = ctx.host.camera_forward().normalize();

		let position = ctx.core.snap_to_grid(
			ctx.host
				.raycast(origin, forward * AIM_RANGE)
				.map_or(origin + forward * AIM_FALLBACK_DISTANCE, |hit| hit.point),
		);

		let light = SpotLight {
			name: format!("Light_{}", ctx.host.light_count()),
			position,
			direction: geom::snap_direction(Vec3f::new(0.0, -1.0, 0.0), ctx.core.angle_snap_step()),
			radius: NEW_LIGHT_RADIUS,
			inner_angle: 30.0f32.to_radians(),
			outer_angle: 40.0f32.to_radians(),
			cast_shadows: true,
			..SpotLight::default()
		};

		let created = ctx.core.push(
			ctx.host,
			EditOp::LightCreate {
				snapshot: light,
				light: None,
			},
			1,
		);

		if let Some(id) = created.light() {
			self.select(ctx, Some(id));
		}
	}

	fn tick(&mut self, ctx: &mut ToolCtx, delta_time: f32) {
		let mut light = self.selected(ctx);

		if light.is_some() && ctx.host.key_pressed(Key::Tab) {
			self.commit_edit(ctx);
			self.deselect();
			light = None;
		}

		if light.is_some() && ctx.host.combo_pressed(Key::Lctrl, Key::D) {
			self.commit_edit(ctx);
			self.duplicate_selected(ctx);
			light = self.selected(ctx);
		}

		if light.is_some() && ctx.host.key_pressed(Key::Backspace) {
			self.editing = false;
			self.delete_selected(ctx);
			light = None;
		}

		if let Some(light) = light {
			let aiming = ctx.host.key_down(Key::MouseRight);

			if aiming {
				self.begin_edit(ctx, light);
				self.aim_selected(ctx, light);
			}

			if self.moving {
				self.move_selected(ctx, light, delta_time);
			}

			if !aiming && !self.moving {
				self.commit_edit(ctx);
			}
		}

		self.draw_markers(ctx);
	}

	fn draw_markers(&self, ctx: &mut ToolCtx) {
		for id in ctx.host.spot_lights() {
			let Some(light) = ctx.host.spot_light(id) else {
				continue;
			};

			let color = if Some(id) == self.selected {
				SELECTED_LIGHT_COLOR
			} else {
				LIGHT_COLOR
			};

			let tip = light.position + light.direction.normalize() * DIRECTION_LENGTH;

			ctx.host
				.debug_solid_box(light.position, Vec3f::splat(MARKER_HALF_EXTENT), color);
			ctx.host.debug_line(light.position, tip, color);
			ctx.host
				.debug_solid_box(tip, Vec3f::splat(DIRECTION_TIP_HALF_EXTENT), color);
		}
	}

	fn pick(&self, ctx: &ToolCtx) -> Option<LightId> {
		let origin = ctx.host.camera_position();
		let direction = ctx.host.camera_forward().normalize();

		let mut nearest = None;
		let mut nearest_distance = PICK_RANGE;

		for id in ctx.host.spot_lights() {
			let Some(light) = ctx.host.spot_light(id) else {
				continue;
			};

			if let Some(distance) = geom::ray_sphere(origin, direction, light.position, PICK_RADIUS)
				&& distance < nearest_distance
			{
				nearest = Some(id);
				nearest_distance = distance;
			}
		}

		nearest
	}

	fn select(&mut self, ctx: &mut ToolCtx, light: Option<LightId>) {
		self.commit_edit(ctx);

		self.selected = light;
		self.moving = false;
	}

	fn deselect(&mut self) {
		self.selected = None;
		self.moving = false;
		self.editing = false;
	}

	fn delete_selected(&mut self, ctx: &mut ToolCtx) {
		let Some(light) = self.selected(ctx) else {
			return;
		};

		if let Some(snapshot) = light_snapshot(ctx.host, light) {
			ctx.core
				.push(ctx.host, EditOp::LightDelete { light, snapshot }, 1);
		}

		self.deselect();
	}

	fn duplicate_selected(&mut self, ctx: &mut ToolCtx) {
		let Some(light) = self.selected(ctx) else {
			return;
		};

		let Some(mut snapshot) = light_snapshot(ctx.host, light) else {
			return;
		};

		let mut index = ctx.host.light_count();

		let name = loop {
			let candidate = format!("Light_{index}");

			if !ctx.host.light_name_in_use(&candidate) {
				break candidate;
			}

			index += 1;
		};

		snapshot.name = name;

		let created = ctx.core.push(
			ctx.host,
			EditOp::LightCreate {
				snapshot,
				light: None,
			},
			1,
		);

		if let Some(id) = created.light() {
			self.select(ctx, Some(id));
		}
	}

	fn begin_edit(&mut self, ctx: &ToolCtx, light: LightId) {
		if self.editing {
			return;
		}

		let Some(state) = ctx.host.spot_light(light) else {
			return;
		};

		self.editing = true;
		self.position_before = state.position;
		self.direction_before = state.direction.normalize();
	}

	fn commit_edit(&mut self, ctx: &mut ToolCtx) {
		let light = if self.editing {
			self.selected(ctx)
		} else {
			None
		};

		self.editing = false;

		let Some(light) = light else {
			return;
		};

		let Some(state) = ctx.host.spot_light(light) else {
			return;
		};

		let position_after = state.position;
		let direction_after = state.direction.normalize();

		if position_after.is_close_to(&self.position_before, EDIT_TOLERANCE)
			&& direction_after.is_close_to(&self.direction_before, EDIT_TOLERANCE)
		{
			return;
		}

		ctx.core.push(
			ctx.host,
			EditOp::LightTransform {
				light,
				position_before: self.position_before,
				position_after,
				direction_before: self.direction_before,
				direction_after,
			},
			1,
		);
	}

	fn move_selected(&mut self, ctx: &mut ToolCtx, light: LightId, delta_time: f32) {
		let player_delta = ctx.host.player_position() - self.player_origin;
		let height_speed = delta_time * ctx.core.snap_step() * HEIGHT_STEPS_PER_SECOND;

		if ctx.host.key_down(Key::E) {
			self.height_offset += height_speed;
		}
		if ctx.host.key_down(Key::Q) {
			self.height_offset -= height_speed;
		}

		let movement = ctx.core.snap_to_grid(Vec3f::new(
			player_delta.x,
			self.height_offset,
			player_delta.z,
		));

		ctx.host
			.set_light_position(light, self.move_origin + movement);
	}

	fn aim_selected(&mut self, ctx: &mut ToolCtx, light: LightId) {
		let Some(state) = ctx.host.spot_light(light) else {
			return;
		};

		let origin = ctx.host.camera_position();
		let forward = ctx.host.camera_forward().normalize();

		let target = ctx
			.host
			.raycast(origin, forward * AIM_RANGE)
			.map_or(origin + forward * AIM_FALLBACK_DISTANCE, |hit| hit.point);

		let to_target = target - state.position;

		if to_target.length() < MIN_AIM_DISTANCE {
			return;
		}

		ctx.host.set_light_direction(
			light,
			geom::snap_direction(to_target, ctx.core.angle_snap_step()),
		);
	}
}

impl Tool for LightTool {
	fn enter(&mut self, _ctx: &mut ToolCtx) {
		self.deselect();
	}

	fn leave(&mut self, _ctx: &mut ToolCtx) {
		self.deselect();
	}

	fn begin(&mut self, ctx: &mut ToolCtx) {
		let picked = self.pick(ctx);

		if picked.is_some() && picked != self.selected {
			self.select(ctx, picked);
		}

		let Some(light) = self.selected(ctx) else {
			return;
		};

		let Some(state) = ctx.host.spot_light(light) else {
			return;
		};

		self.moving = true;
		self.move_origin = state.position;
		self.player_origin = ctx.host.player_position();
		self.height_offset = 0.0;

		self.begin_edit(ctx, light);
	}

	fn update(&mut self, ctx: &mut ToolCtx, delta_time: f32) {
		self.tick(ctx, delta_time);
	}

	fn finalize(&mut self, _ctx: &mut ToolCtx) {
		self.moving = false;
	}

	fn cancel(&mut self, _ctx: &mut ToolCtx) {
		self.moving = false;
		self.editing = false;
	}

	fn controls(&mut self, ctx: &mut ToolCtx) {
		self.tick(ctx, 0.0);
	}
}
