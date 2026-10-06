use raptor_math::Vec3f;

use super::{TRANSFORM_EPSILON, Tool, ToolCtx};
use crate::history::EditOp;
use crate::host::ObjectId;
use crate::key::Key;

const HEIGHT_STEPS_PER_SECOND: f32 = 8.0;

#[derive(Default)]
pub struct TranslateTool {
	initial_player: Vec3f,
	height: f32,
	initial: Vec<(ObjectId, Vec3f)>,
}

impl Tool for TranslateTool {
	fn begin(&mut self, ctx: &mut ToolCtx) {
		self.initial_player = ctx.host.player_position();
		self.height = 0.0;

		self.initial = ctx
			.core
			.selection
			.objects()
			.map(|object| (object, ctx.host.object_position(object)))
			.collect();
	}

	fn update(&mut self, ctx: &mut ToolCtx, delta_time: f32) {
		if self.initial.is_empty() {
			return;
		}

		let player_delta = ctx.host.player_position() - self.initial_player;
		let speed = delta_time * ctx.core.snap_step() * HEIGHT_STEPS_PER_SECOND;

		if ctx.host.key_down(Key::E) {
			self.height += speed;
		}
		if ctx.host.key_down(Key::Q) {
			self.height -= speed;
		}

		let movement =
			ctx.core
				.snap_to_grid(Vec3f::new(player_delta.x, self.height, player_delta.z));

		for &(object, initial) in &self.initial {
			ctx.host.set_object_position(object, initial + movement);
		}
	}

	fn finalize(&mut self, ctx: &mut ToolCtx) {
		let moved: Vec<(ObjectId, Vec3f, Vec3f)> = std::mem::take(&mut self.initial)
			.into_iter()
			.filter(|(object, _)| ctx.host.object_exists(*object))
			.map(|(object, before)| (object, before, ctx.host.object_position(object)))
			.filter(|(_, before, after)| !(*before - *after).is_near_zero(TRANSFORM_EPSILON))
			.collect();

		let group = moved.len() as i32;

		for (object, before, after) in moved {
			ctx.core.push(
				ctx.host,
				EditOp::Move {
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
