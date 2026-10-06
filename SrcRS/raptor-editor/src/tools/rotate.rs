use raptor_math::Vec3f;

use super::{TRANSFORM_EPSILON, Tool, ToolCtx};
use crate::geom;
use crate::history::EditOp;
use crate::host::ObjectId;
use crate::key::Key;

const SENSITIVITY: f32 = std::f32::consts::FRAC_PI_2;
const SNAP_DEGREES: [f32; 4] = [1.0, 5.0, 15.0, 45.0];
const PITCH_RATE: f32 = 4.0;

#[derive(Default)]
pub struct RotateTool {
	initial_player: Vec3f,
	pitch: f32,
	initial: Vec<(ObjectId, Vec3f)>,
}

fn snap_radians(level: i32) -> f32 {
	SNAP_DEGREES[level.clamp(0, SNAP_DEGREES.len() as i32 - 1) as usize].to_radians()
}

impl Tool for RotateTool {
	fn begin(&mut self, ctx: &mut ToolCtx) {
		self.initial_player = ctx.host.player_position();
		self.pitch = 0.0;

		self.initial = ctx
			.core
			.selection
			.objects()
			.map(|object| (object, ctx.host.object_euler(object)))
			.collect();
	}

	fn update(&mut self, ctx: &mut ToolCtx, delta_time: f32) {
		if self.initial.is_empty() {
			return;
		}

		let player_delta = ctx.host.player_position() - self.initial_player;
		let yaw = player_delta.x * SENSITIVITY;

		let step = snap_radians(ctx.core.snap_level);
		let pitch_speed = delta_time * step * PITCH_RATE;

		if ctx.host.key_down(Key::Q) {
			self.pitch -= pitch_speed;
		}
		if ctx.host.key_down(Key::E) {
			self.pitch += pitch_speed;
		}

		let mut delta = Vec3f::new(self.pitch, yaw, 0.0);

		if ctx.core.snap_enabled {
			delta = geom::round_to_step(delta, step);
		}

		for &(object, initial) in &self.initial {
			ctx.host.set_object_euler(object, initial + delta);
		}
	}

	fn finalize(&mut self, ctx: &mut ToolCtx) {
		let moved: Vec<(ObjectId, Vec3f, Vec3f)> = std::mem::take(&mut self.initial)
			.into_iter()
			.filter(|(object, _)| ctx.host.object_exists(*object))
			.map(|(object, before)| (object, before, ctx.host.object_euler(object)))
			.filter(|(_, before, after)| !(*before - *after).is_near_zero(TRANSFORM_EPSILON))
			.collect();

		let group = moved.len() as i32;

		for (object, before, after) in moved {
			ctx.core.push(
				ctx.host,
				EditOp::Rotate {
					object,
					before,
					after,
				},
				group,
			);
		}
	}

	fn cancel(&mut self, _ctx: &mut ToolCtx) {
		self.initial.clear();
	}
}
