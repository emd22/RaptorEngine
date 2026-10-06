use raptor_brush::Brush;
use raptor_math::Vec3f;

use super::{Tool, ToolCtx};
use crate::blockout::{clip, ray_face};
use crate::geom::{self, transform_direction};
use crate::history::{EditOp, ObjectSnapshot};
use crate::host::{BlockSnapshot, ObjectId, TAG_PROBE_VOLUME, TAG_REFLECTION_PROBE};

#[derive(Default)]
pub struct ClipTool {
	active: bool,
	object: Option<ObjectId>,
	start: Vec3f,
	end: Vec3f,
	normal: Vec3f,
	face_point: Vec3f,
}

impl ClipTool {
	fn snap_on_face(&self, ctx: &ToolCtx, point: Vec3f) -> Vec3f {
		let snapped = ctx.core.snap_to_grid(point);

		snapped - self.normal * (snapped - self.face_point).dot(&self.normal)
	}

	fn has_line(&self) -> bool {
		let line = self.end - self.start;

		line.dot(&line) > 0.0001
	}
}

impl Tool for ClipTool {
	fn begin(&mut self, ctx: &mut ToolCtx) {
		self.active = false;

		let Some(object) = ctx.core.selection.get(0) else {
			return;
		};

		let Some(hit) = ray_face(
			ctx.host,
			object,
			ctx.host.camera_position(),
			ctx.host.camera_forward(),
		) else {
			return;
		};

		self.object = Some(object);
		self.normal = transform_direction(&ctx.host.object_world_matrix(object), hit.local_normal)
			.normalize();
		self.face_point = hit.point;
		self.start = self.snap_on_face(ctx, hit.point);
		self.end = self.start;
		self.active = true;
	}

	fn update(&mut self, ctx: &mut ToolCtx, _delta_time: f32) {
		let Some(object) = self.object.filter(|_| self.active) else {
			return;
		};

		self.end = self.snap_on_face(
			ctx,
			geom::ray_to_plane(
				ctx.host.camera_position(),
				ctx.host.camera_forward(),
				self.face_point,
				self.normal,
			),
		);

		let pieces = self
			.has_line()
			.then(|| clip(ctx.host, object, self.start, self.end, self.normal))
			.flatten();

		match pieces {
			Some(pieces) => {
				let rotation = ctx.host.object_rotation(object);

				ctx.host
					.show_preview(pieces.split_position, rotation, &pieces.split);
			}
			None => ctx.host.hide_preview(),
		}
	}

	fn finalize(&mut self, ctx: &mut ToolCtx) {
		ctx.host.hide_preview();

		let active = std::mem::take(&mut self.active);

		let Some(object) = self.object.take().filter(|_| active && self.has_line()) else {
			return;
		};

		if !ctx.host.object_exists(object) {
			return;
		}

		let Some(before) = ctx.host.brush_planes(object) else {
			return;
		};

		let Some(pieces) = clip(ctx.host, object, self.start, self.end, self.normal) else {
			return;
		};

		if !Brush::from_planes(&pieces.kept).is_valid()
			|| !Brush::from_planes(&pieces.split).is_valid()
		{
			return;
		}

		let tags = ctx.host.object_tags(object);

		let snapshot = ObjectSnapshot {
			block: BlockSnapshot {
				name: String::new(),
				position: pieces.split_position,
				rotation: ctx.host.object_rotation(object),
				material: ctx.core.selection.stored_material(ctx.host, object),
				is_probe_volume: tags & TAG_PROBE_VOLUME != 0,
				is_reflection_probe: tags & TAG_REFLECTION_PROBE != 0,
				is_dynamic: ctx.host.block_is_dynamic(object),
			},
			planes: pieces.split,
		};

		ctx.core.push(
			ctx.host,
			EditOp::BrushEdit {
				object,
				before,
				after: pieces.kept,
			},
			2,
		);

		ctx.core.push(
			ctx.host,
			EditOp::CreateBrush {
				snapshot,
				object: None,
			},
			2,
		);
	}

	fn cancel(&mut self, ctx: &mut ToolCtx) {
		self.active = false;
		ctx.host.hide_preview();
	}

	fn leave(&mut self, ctx: &mut ToolCtx) {
		ctx.host.hide_preview();
	}
}
