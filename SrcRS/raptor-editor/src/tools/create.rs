use raptor_brush::edit::world_box;
use raptor_math::Vec3f;
use raptor_math::mat4::Quat;

use super::{Tool, ToolCtx};
use crate::blockout::is_valid_brush;
use crate::geom;
use crate::history::{EditOp, ObjectSnapshot};
use crate::host::BlockSnapshot;
use crate::key::Key;

const CREATE_RANGE: f32 = 100.0;
const DEFAULT_HEIGHT: f32 = 1.0;

#[derive(Default)]
pub struct CreateTool {
	active: bool,
	surface: Vec3f,
	start: Vec3f,
	normal: Vec3f,
	height: f32,
	min: Vec3f,
	max: Vec3f,
}

impl CreateTool {
	fn cell(&self, ctx: &ToolCtx) -> Vec3f {
		let mut point = geom::ray_to_plane(
			ctx.host.camera_position(),
			ctx.host.camera_forward(),
			self.surface,
			self.normal,
		);

		if ctx.core.snap_enabled {
			point = geom::floor_to_step(point, ctx.core.snap_step());
		}

		let mask = self.normal.abs();

		point * (Vec3f::ONE - mask) + self.surface * mask
	}
}

impl Tool for CreateTool {
	fn begin(&mut self, ctx: &mut ToolCtx) {
		let origin = ctx.host.camera_position();

		let Some(hit) = ctx
			.host
			.raycast(origin, ctx.host.camera_forward() * CREATE_RANGE)
		else {
			self.active = false;
			return;
		};

		self.active = true;
		self.normal = geom::dominant_axis(hit.normal);

		let snapped = ctx.core.snap_to_grid(hit.point);

		let mut surface = hit.point;

		if (snapped - hit.point).abs().dot(&self.normal.abs()) < 0.001 {
			surface = snapped;
		}

		self.surface = surface;
		self.start = self.cell(ctx);
		self.height = DEFAULT_HEIGHT;
	}

	fn update(&mut self, ctx: &mut ToolCtx, _delta_time: f32) {
		if !self.active {
			return;
		}

		let step = ctx.core.snap_step();

		if ctx.host.key_pressed(Key::E) {
			self.height += step;
		}
		if ctx.host.key_pressed(Key::Q) {
			self.height = step.max(self.height - step);
		}

		let plane_mask = Vec3f::ONE - self.normal.abs();

		let cell_size = if ctx.core.snap_enabled {
			plane_mask * step
		} else {
			Vec3f::ZERO
		};

		let corner = self.cell(ctx);
		let top = self.start + self.normal * self.height;

		let low = self.start.min(&corner).min(&top);
		let mut high = self.start.max(&corner) + cell_size;

		high = high.max(&top);
		high = high.max(&(low + Vec3f::splat(step)));

		self.min = low;
		self.max = high;

		let (planes, position) = world_box(self.min, self.max);

		ctx.host.show_preview(position, Quat::IDENTITY, &planes);
	}

	fn finalize(&mut self, ctx: &mut ToolCtx) {
		ctx.host.hide_preview();

		if !std::mem::take(&mut self.active) {
			return;
		}

		let (planes, position) = world_box(self.min, self.max);

		if !is_valid_brush(&planes) {
			return;
		}

		let snapshot = ObjectSnapshot {
			block: BlockSnapshot {
				name: String::new(),
				position,
				rotation: Quat::IDENTITY,
				material: ctx.host.default_material(),
				is_probe_volume: false,
				is_reflection_probe: false,
				is_dynamic: false,
			},
			planes,
		};

		let created = ctx.core.push(
			ctx.host,
			EditOp::CreateBrush {
				snapshot,
				object: None,
			},
			1,
		);

		if let Some(object) = created.object() {
			ctx.core.select_object(ctx.host, object, false);
		}
	}

	fn cancel(&mut self, ctx: &mut ToolCtx) {
		self.active = false;
		ctx.host.hide_preview();
	}

	fn leave(&mut self, ctx: &mut ToolCtx) {
		ctx.host.hide_preview();
	}
}
