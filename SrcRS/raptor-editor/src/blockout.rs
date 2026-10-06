use raptor_brush::edit::{ClipPieces, clip_pieces};
use raptor_brush::{Brush, Plane};
use raptor_math::{Mat4f, Vec3f};

use crate::geom::{transform_direction, transform_point};
use crate::host::{EditorHost, ObjectHost, ObjectId, TAG_PROBE_VOLUME};

#[derive(Clone, Copy, Debug)]
pub struct FaceHit {
	pub local_normal: Vec3f,
	pub point: Vec3f,
	pub distance: f32,
}

pub fn world_to_local(host: &dyn ObjectHost, object: ObjectId) -> Mat4f {
	host.object_world_matrix(object).inverse()
}

pub fn ray_face(
	host: &dyn EditorHost,
	object: ObjectId,
	origin: Vec3f,
	direction: Vec3f,
) -> Option<FaceHit> {
	if host.object_tags(object) & TAG_PROBE_VOLUME != 0 {
		let (distance, face) = host.object_raycast_bounds(object, origin, direction)?;

		return Some(FaceHit {
			local_normal: face,
			point: origin + direction * distance,
			distance,
		});
	}

	let planes = host.brush_planes(object)?;
	let inverse = world_to_local(host, object);

	let local_origin = transform_point(&inverse, origin);
	let local_direction = transform_direction(&inverse, direction);

	let (distance, plane) = Brush::from_planes(&planes).raycast(local_origin, local_direction)?;

	Some(FaceHit {
		local_normal: planes[plane as usize].normal,
		point: origin + direction * distance,
		distance,
	})
}

pub fn brush_support(planes: &[Plane], direction: Vec3f) -> f32 {
	Brush::from_planes(planes).support(direction.normalize())
}

pub fn face_center(planes: &[Plane], face: Vec3f) -> Option<Vec3f> {
	let brush = Brush::from_planes(planes);
	let index = u32::try_from(brush.find_plane(face)).ok()?;

	Some(brush.face_center(index))
}

pub fn clip(
	host: &dyn EditorHost,
	object: ObjectId,
	a: Vec3f,
	b: Vec3f,
	normal: Vec3f,
) -> Option<ClipPieces> {
	let planes = host.brush_planes(object)?;

	clip_pieces(
		&planes,
		&world_to_local(host, object),
		&host.object_rotation(object),
		host.object_position(object),
		a,
		b,
		normal,
	)
}

pub fn is_valid_brush(planes: &[Plane]) -> bool {
	Brush::from_planes(planes).is_valid()
}
