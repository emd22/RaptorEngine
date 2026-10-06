use raptor_math::Vec3f;

use super::{Tool, ToolCtx};
use crate::host::{BodyId, Rgba};

const GRAB_RANGE: f32 = 6.0;
const MIN_HOLD_DISTANCE: f32 = 1.0;
const STIFFNESS: f32 = 20.0;
const MAX_SPEED: f32 = 20.0;
const ANGULAR_DAMPING: f32 = 1.5;

const GRAB_COLOR: Rgba = [255, 120, 40, 255];

#[derive(Default)]
pub struct GrabTool {
	held: Option<Held>,
}

struct Held {
	body: BodyId,
	local_point: Vec3f,
	distance: f32,
}

impl Tool for GrabTool {
	fn enter(&mut self, _ctx: &mut ToolCtx) {
		self.held = None;
	}

	fn leave(&mut self, _ctx: &mut ToolCtx) {
		self.held = None;
	}

	fn begin(&mut self, ctx: &mut ToolCtx) {
		self.held = None;

		let origin = ctx.host.camera_position();

		let Some(hit) = ctx
			.host
			.raycast(origin, ctx.host.camera_forward() * GRAB_RANGE)
		else {
			return;
		};

		let Some(body) = hit.body.filter(|body| ctx.host.body_is_dynamic(*body)) else {
			return;
		};

		self.held = Some(Held {
			body,
			local_point: ctx.host.body_point_to_local(body, hit.point),
			distance: (hit.point - origin).length().max(MIN_HOLD_DISTANCE),
		});
	}

	fn update(&mut self, ctx: &mut ToolCtx, _delta_time: f32) {
		let Some(held) = &self.held else {
			return;
		};

		let target = ctx.host.camera_position() + ctx.host.camera_forward() * held.distance;

		let point = ctx.host.body_hold(
			held.body,
			held.local_point,
			target,
			STIFFNESS,
			MAX_SPEED,
			ANGULAR_DAMPING,
		);

		match point {
			Some(point) => ctx.host.debug_line(point, target, GRAB_COLOR),
			None => self.held = None,
		}
	}

	fn finalize(&mut self, _ctx: &mut ToolCtx) {
		self.held = None;
	}

	fn cancel(&mut self, _ctx: &mut ToolCtx) {
		self.held = None;
	}
}
