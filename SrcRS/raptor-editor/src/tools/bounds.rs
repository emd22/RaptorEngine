use raptor_math::{Aabb, Vec3f};

use super::{Tool, ToolCtx};
use crate::geom::{self, axis, has_volume, set_axis, transform_direction};
use crate::history::EditOp;
use crate::host::{ObjectId, Rgba, TAG_BLOCKOUT};

const MIN_EXTENT: f32 = 0.05;
const EDIT_TOLERANCE: f32 = 0.00001;

const BOUNDS_COLOR: Rgba = [80, 220, 255, 255];
const FACE_COLOR: Rgba = [255, 120, 40, 255];

#[derive(Default)]
pub struct BoundsTool {
	active: bool,
	object: Option<ObjectId>,
	bounds_before: Aabb,
	axis: usize,
	sign: f32,
	player_origin: Vec3f,
	world_normal: Vec3f,
	axis_scale: f32,
}

#[derive(Clone, Copy)]
struct Target {
	object: ObjectId,
	face: Vec3f,
}

fn face_axis(face: Vec3f) -> usize {
	if face.x.abs() > 0.5 {
		0
	} else if face.y.abs() > 0.5 {
		1
	} else {
		2
	}
}

fn find_nearest(
	ctx: &ToolCtx,
	node: ObjectId,
	origin: Vec3f,
	direction: Vec3f,
	nearest: &mut f32,
	out: &mut Option<Target>,
) {
	if ctx.host.object_has_mesh(node) && has_volume(&ctx.host.object_bounds(node)) {
		if let Some((distance, face)) = ctx.host.object_raycast_bounds(node, origin, direction)
			&& distance >= 0.0
			&& distance < *nearest
		{
			*nearest = distance;
			*out = Some(Target { object: node, face });
		}
	}

	for child in ctx.host.object_children(node) {
		if ctx.host.object_exists(child) {
			find_nearest(ctx, child, origin, direction, nearest, out);
		}
	}
}

fn find_target(ctx: &ToolCtx) -> Option<Target> {
	let origin = ctx.host.camera_position();
	let direction = ctx.host.camera_forward();

	let mut nearest = f32::MAX;
	let mut target = None;

	for root in ctx.core.selection.objects() {
		if ctx.host.object_tags(root) & TAG_BLOCKOUT != 0 {
			continue;
		}

		find_nearest(ctx, root, origin, direction, &mut nearest, &mut target);
	}

	target
}

fn draw_target(ctx: &mut ToolCtx, object: ObjectId, face: Vec3f) {
	let world = ctx.host.object_world_matrix(object);
	let bounds = ctx.host.object_bounds(object);

	let half_extent = (bounds.max - bounds.min) * 0.5;
	let center = (bounds.max + bounds.min) * 0.5;

	let transform = raptor_math::Mat4f::as_scale(half_extent)
		* raptor_math::Mat4f::as_translation(center)
		* world;

	ctx.host.debug_wire_box(&transform, BOUNDS_COLOR);

	let face_axis = face_axis(face);
	let sign = if axis(face, face_axis) > 0.0 {
		1.0
	} else {
		-1.0
	};

	let mut face_half_extent = half_extent;
	set_axis(&mut face_half_extent, face_axis, 0.0);

	let mut face_center = center;
	let face_offset = axis(face_center, face_axis) + sign * axis(half_extent, face_axis);
	set_axis(&mut face_center, face_axis, face_offset);

	let face_transform = raptor_math::Mat4f::as_scale(face_half_extent)
		* raptor_math::Mat4f::as_translation(face_center)
		* world;

	ctx.host.debug_wire_box(&face_transform, FACE_COLOR);
}

impl BoundsTool {
	fn resolve(&self, ctx: &ToolCtx) -> Option<ObjectId> {
		self.object
			.filter(|object| self.active && ctx.host.object_exists(*object))
	}
}

impl Tool for BoundsTool {
	fn enter(&mut self, _ctx: &mut ToolCtx) {
		self.active = false;
	}

	fn leave(&mut self, _ctx: &mut ToolCtx) {
		self.active = false;
	}

	fn controls(&mut self, ctx: &mut ToolCtx) {
		if let Some(target) = find_target(ctx) {
			draw_target(ctx, target.object, target.face);
		}
	}

	fn begin(&mut self, ctx: &mut ToolCtx) {
		self.active = false;

		let Some(target) = find_target(ctx) else {
			return;
		};

		self.object = Some(target.object);
		self.bounds_before = ctx.host.object_bounds(target.object);

		self.axis = face_axis(target.face);
		self.sign = if axis(target.face, self.axis) > 0.0 {
			1.0
		} else {
			-1.0
		};

		let world = ctx.host.object_world_matrix(target.object);
		let world_axis = transform_direction(&world, target.face);

		self.axis_scale = world_axis.length().max(1e-4);
		self.world_normal = world_axis / self.axis_scale;
		self.player_origin = ctx.host.player_position();
		self.active = true;
	}

	fn update(&mut self, ctx: &mut ToolCtx, _delta_time: f32) {
		let Some(object) = self.resolve(ctx) else {
			self.active = false;
			return;
		};

		let projected = (ctx.host.player_position() - self.player_origin).dot(&self.world_normal);
		let snapped = ctx.core.snap_to_grid(Vec3f::splat(projected)).x;

		let extent_before =
			axis(self.bounds_before.max, self.axis) - axis(self.bounds_before.min, self.axis);
		let amount = (snapped / self.axis_scale).max(MIN_EXTENT - extent_before);

		let mut bounds = self.bounds_before;

		if self.sign > 0.0 {
			let value = axis(bounds.max, self.axis) + amount;
			set_axis(&mut bounds.max, self.axis, value);
		} else {
			let value = axis(bounds.min, self.axis) - amount;
			set_axis(&mut bounds.min, self.axis, value);
		}

		if !geom::bounds_equal(&ctx.host.object_bounds(object), &bounds, EDIT_TOLERANCE) {
			ctx.host.set_object_bounds(object, bounds);
		}

		let mut face = Vec3f::ZERO;
		set_axis(&mut face, self.axis, self.sign);

		draw_target(ctx, object, face);
	}

	fn finalize(&mut self, ctx: &mut ToolCtx) {
		let object = self.resolve(ctx);

		self.active = false;

		let Some(object) = object else {
			return;
		};

		let after = ctx.host.object_bounds(object);

		if geom::bounds_equal(&after, &self.bounds_before, EDIT_TOLERANCE) {
			return;
		}

		ctx.core.push(
			ctx.host,
			EditOp::BoundsEdit {
				object,
				before: self.bounds_before,
				after,
			},
			1,
		);
	}

	fn cancel(&mut self, ctx: &mut ToolCtx) {
		let object = self.resolve(ctx);

		self.active = false;

		if let Some(object) = object {
			ctx.host.set_object_bounds(object, self.bounds_before);
		}
	}
}
