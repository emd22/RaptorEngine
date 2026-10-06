use raptor_brush::edit::{FaceTextureEdit, edit_face_texture};
use raptor_math::Vec3f;

use super::{TRANSFORM_EPSILON, Tool, ToolCtx};
use crate::blockout::{brush_support, face_center, ray_face};
use crate::geom::{transform_direction, transform_point};
use crate::history::{EditOp, MIN_BLOCKOUT_THICKNESS};
use crate::host::ObjectId;
use crate::key::Key;

const TEXTURE_ROTATE_STEP: f32 = 15.0;
const TEXTURE_SCALE_STEP: f32 = 2.0;

#[derive(Default)]
pub struct FaceTool {
	active: bool,
	initial_player: Vec3f,
	face: Vec3f,
	initial_face_position: Vec3f,
	world_normal: Vec3f,
	min_projection: f32,
	magnitude: f32,
}

impl FaceTool {
	fn edit_texture(
		ctx: &mut ToolCtx,
		object: ObjectId,
		face: Vec3f,
		edit: FaceTextureEdit,
		amount: [f32; 2],
	) {
		let Some(before) = ctx.host.brush_planes(object) else {
			return;
		};

		let Some(after) = edit_face_texture(&before, face, edit, amount) else {
			return;
		};

		ctx.core.push(
			ctx.host,
			EditOp::BrushEdit {
				object,
				before,
				after,
			},
			1,
		);
	}
}

impl Tool for FaceTool {
	fn begin(&mut self, ctx: &mut ToolCtx) {
		self.active = false;
		self.magnitude = 0.0;
		self.initial_player = ctx.host.player_position();

		let Some(last) = ctx.core.selection.last() else {
			return;
		};

		let Some(hit) = ray_face(
			ctx.host,
			last,
			ctx.host.camera_position(),
			ctx.host.camera_forward(),
		) else {
			return;
		};

		let Some(planes) = ctx.host.brush_planes(last) else {
			return;
		};

		let Some(center) = face_center(&planes, hit.local_normal) else {
			return;
		};

		let world = ctx.host.object_world_matrix(last);

		self.face = hit.local_normal;
		self.initial_face_position = transform_point(&world, center);
		self.world_normal = transform_direction(&world, self.face).normalize();

		let thickness =
			brush_support(&planes, self.face) + brush_support(&planes, self.face * -1.0);

		self.min_projection = (MIN_BLOCKOUT_THICKNESS - thickness).min(0.0);
		self.active = true;
	}

	fn update(&mut self, ctx: &mut ToolCtx, _delta_time: f32) {
		if !self.active {
			return;
		}

		let travelled = (ctx.host.player_position() - self.initial_player).dot(&self.world_normal);
		let snapped = ctx.core.snap_to_grid(Vec3f::splat(travelled)).x;

		self.magnitude = snapped.max(self.min_projection);

		ctx.host.set_transform_marker(Some(
			self.initial_face_position + self.world_normal * self.magnitude,
		));
	}

	fn finalize(&mut self, ctx: &mut ToolCtx) {
		ctx.host.set_transform_marker(None);

		let active = std::mem::take(&mut self.active);

		if !active || self.magnitude.abs() < TRANSFORM_EPSILON {
			return;
		}

		let objects: Vec<ObjectId> = ctx.core.selection.objects().collect();
		let group = objects.len() as i32;

		for object in objects {
			ctx.core.push(
				ctx.host,
				EditOp::Scale {
					object,
					face: self.face,
					amount: self.magnitude,
					planes_before: Vec::new(),
				},
				group,
			);

			ctx.host.rebuild_block(object);
		}
	}

	fn cancel(&mut self, ctx: &mut ToolCtx) {
		self.active = false;
		ctx.host.set_transform_marker(None);
	}

	fn leave(&mut self, ctx: &mut ToolCtx) {
		ctx.host.set_transform_marker(None);
	}

	fn controls(&mut self, ctx: &mut ToolCtx) {
		let Some(last) = ctx.core.selection.last() else {
			return;
		};

		let Some(hit) = ray_face(
			ctx.host,
			last,
			ctx.host.camera_position(),
			ctx.host.camera_forward(),
		) else {
			return;
		};

		let face = hit.local_normal;
		let step = ctx.core.snap_step();

		let shifts = [
			(Key::Left, [-step, 0.0]),
			(Key::Right, [step, 0.0]),
			(Key::Up, [0.0, -step]),
			(Key::Down, [0.0, step]),
		];

		for (key, amount) in shifts {
			if ctx.host.key_pressed(key) {
				Self::edit_texture(ctx, last, face, FaceTextureEdit::Shift, amount);
			}
		}

		if ctx.host.key_pressed(Key::Lbracket) {
			let shrink = 1.0 / TEXTURE_SCALE_STEP;
			Self::edit_texture(ctx, last, face, FaceTextureEdit::Scale, [shrink, shrink]);
		}
		if ctx.host.key_pressed(Key::Rbracket) {
			Self::edit_texture(
				ctx,
				last,
				face,
				FaceTextureEdit::Scale,
				[TEXTURE_SCALE_STEP, TEXTURE_SCALE_STEP],
			);
		}

		if ctx.host.key_pressed(Key::Comma) {
			Self::edit_texture(
				ctx,
				last,
				face,
				FaceTextureEdit::Rotate,
				[-TEXTURE_ROTATE_STEP, 0.0],
			);
		}
		if ctx.host.key_pressed(Key::Period) {
			Self::edit_texture(
				ctx,
				last,
				face,
				FaceTextureEdit::Rotate,
				[TEXTURE_ROTATE_STEP, 0.0],
			);
		}

		if ctx.host.key_pressed(Key::J) {
			Self::edit_texture(ctx, last, face, FaceTextureEdit::Reset, [0.0, 0.0]);
		}
	}
}
