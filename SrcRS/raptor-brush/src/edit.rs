use raptor_math::mat4::Quat;
use raptor_math::{Mat4f, Vec3f, Vec4f};

use crate::{Brush, Plane};

const FULL_TURN_DEGREES: f32 = 360.0;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum FaceTextureEdit
{
	Shift = 0,
	Scale = 1,
	Rotate = 2,
	Reset = 3,
}

impl FaceTextureEdit
{
	pub fn from_u32(value: u32) -> Option<Self>
	{
		match value {
			0 => Some(Self::Shift),
			1 => Some(Self::Scale),
			2 => Some(Self::Rotate),
			3 => Some(Self::Reset),
			_ => None,
		}
	}
}

pub struct ClipPieces
{
	pub kept: Vec<Plane>,
	pub split: Vec<Plane>,
	pub split_position: Vec3f,
}

pub fn quat_from_array(values: [f32; 4]) -> Quat
{
	Quat {
		x: values[0],
		y: values[1],
		z: values[2],
		w: values[3],
	}
}

/// Where a brush that is turned about the object's origin has to move so that the parts it keeps
/// stay put when its centre moves.
pub fn keep_in_place_offset(rotation: &Quat, old_center: Vec3f, new_center: Vec3f) -> Vec3f
{
	let center_delta = new_center - old_center;

	center_delta.rotate(rotation) - center_delta
}

/// The planes of a brush with one face pushed out by `distance`, kept at least `min_thickness`
/// thick behind the face. `None` if there is no such face or the result is not a valid brush.
pub fn move_face(
	planes: &[Plane],
	normal: Vec3f,
	distance: f32,
	min_thickness: f32,
) -> Option<Vec<Plane>>
{
	let brush = Brush::from_planes(planes);
	let index = usize::try_from(brush.find_plane(normal)).ok()?;

	let mut moved = brush.planes.clone();
	let plane = &mut moved[index];

	let thickness = plane.distance + brush.support(plane.normal * -1.0);
	plane.distance += distance.max(min_thickness - thickness);

	Brush::from_planes(&moved).is_valid().then_some(moved)
}

pub fn edit_face_texture(
	planes: &[Plane],
	normal: Vec3f,
	edit: FaceTextureEdit,
	amount: [f32; 2],
) -> Option<Vec<Plane>>
{
	let brush = Brush::from_planes(planes);
	let index = usize::try_from(brush.find_plane(normal)).ok()?;

	let mut edited = planes.to_vec();
	let texture = &mut edited[index].texture;

	match edit {
		FaceTextureEdit::Shift => {
			texture.offset = [texture.offset[0] + amount[0], texture.offset[1] + amount[1]];
		}
		FaceTextureEdit::Scale => {
			texture.scale = [texture.scale[0] * amount[0], texture.scale[1] * amount[1]];
		}
		FaceTextureEdit::Rotate => {
			texture.rotation =
				(texture.rotation + amount[0] + FULL_TURN_DEGREES) % FULL_TURN_DEGREES;
		}
		FaceTextureEdit::Reset => {
			let mut reset = Brush::from_planes(&edited);
			reset.reset_face_texture(index);
			edited = reset.planes;
		}
	}

	Some(edited)
}

/// How a blockout splits along the plane through two points drawn on a face. `to_local` takes the
/// world to the object's space, and everything else is in the world.
pub fn clip_pieces(
	planes: &[Plane],
	to_local: &Mat4f,
	rotation: &Quat,
	position: Vec3f,
	point_a: Vec3f,
	point_b: Vec3f,
	face_normal: Vec3f,
) -> Option<ClipPieces>
{
	let brush = Brush::from_planes(planes);

	let local_a = (*to_local * Vec4f::new(point_a.x, point_a.y, point_a.z, 1.0)).xyz();
	let local_b = (*to_local * Vec4f::new(point_b.x, point_b.y, point_b.z, 1.0)).xyz();
	let local_normal =
		(*to_local * Vec4f::new(face_normal.x, face_normal.y, face_normal.z, 0.0)).xyz();

	let cut_normal = (local_b - local_a).cross(&local_normal);

	if cut_normal.is_near_zero(0.00001) {
		return None;
	}

	let normal = cut_normal.normalize();

	let (kept, split) = brush.split(normal, normal.dot(&local_a), position)?;

	let split_center = Brush::from_planes(&split).center();
	let split_position = position + keep_in_place_offset(rotation, brush.center(), split_center);

	Some(ClipPieces {
		kept,
		split,
		split_position,
	})
}

/// A box filling `min` to `max` in the world, with its textures lined up with the world grid, and
/// where its object goes.
pub fn world_box(min: Vec3f, max: Vec3f) -> (Vec<Plane>, Vec3f)
{
	let position = (min + max) * 0.5;
	let half_size = (max - min) * 0.5;

	let mut brush = Brush::from_box(half_size * -1.0, half_size);
	brush.align_textures_to_world(position);

	(brush.planes, position)
}

/// The planes a level file describes: the faces of a box, or the listed planes, with the default
/// texture layout where the file gave none.
pub fn brush_planes(planes: &[Plane], has_textures: bool) -> Vec<Plane>
{
	let mut brush = Brush::from_planes(planes);

	if !has_textures {
		for index in 0..brush.planes.len() {
			brush.reset_face_texture(index);
		}
	}

	brush.planes
}

pub fn same_planes(a: &[Plane], b: &[Plane]) -> bool
{
	a.len() == b.len()
		&& a.iter()
			.zip(b)
			.all(|(a, b)| a.normal.to_array() == b.normal.to_array() && a.distance == b.distance)
}

/// Whether a saved blockout is written as a box (and whether it carries its textures) or as planes.
pub fn save_shape(planes: &[Plane]) -> SaveShape
{
	let brush = Brush::from_planes(planes);
	let default_textures = brush.has_default_textures();

	SaveShape {
		as_box: brush.is_box() && default_textures,
		textures: !default_textures,
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SaveShape
{
	pub as_box: bool,
	pub textures: bool,
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn unit_box() -> Vec<Plane>
	{
		Brush::from_box(Vec3f::new(-1.0, -1.0, -1.0), Vec3f::new(1.0, 1.0, 1.0)).planes
	}

	#[test]
	fn moving_a_face_pushes_its_plane_out()
	{
		let moved = move_face(&unit_box(), Vec3f::new(1.0, 0.0, 0.0), 0.5, 0.1).unwrap();
		let brush = Brush::from_planes(&moved);

		assert!((brush.bounds_max.x - 1.5).abs() < 1e-5);
		assert!((brush.bounds_min.x + 1.0).abs() < 1e-5);
	}

	#[test]
	fn a_face_cannot_be_pushed_through_the_brush()
	{
		let moved = move_face(&unit_box(), Vec3f::new(1.0, 0.0, 0.0), -5.0, 0.1).unwrap();
		let brush = Brush::from_planes(&moved);

		assert!((brush.bounds_max.x - brush.bounds_min.x - 0.1).abs() < 1e-4);
	}

	#[test]
	fn a_missing_face_gives_nothing()
	{
		assert!(move_face(&unit_box(), Vec3f::new(0.3, 0.9, 0.0).normalize(), 1.0, 0.1).is_none());
	}

	#[test]
	fn texture_edits_change_one_face()
	{
		let normal = Vec3f::new(0.0, 1.0, 0.0);
		let before = unit_box();
		let shifted =
			edit_face_texture(&before, normal, FaceTextureEdit::Shift, [0.5, 1.0]).unwrap();
		let index = Brush::from_planes(&before).find_plane(normal) as usize;

		for (i, (a, b)) in before.iter().zip(&shifted).enumerate() {
			if i == index {
				assert_eq!(b.texture.offset[0], a.texture.offset[0] + 0.5);
			} else {
				assert_eq!(a.texture, b.texture);
			}
		}

		let rotated =
			edit_face_texture(&before, normal, FaceTextureEdit::Rotate, [-90.0, 0.0]).unwrap();

		assert_eq!(rotated[index].texture.rotation, 270.0);
	}

	#[test]
	fn clipping_splits_a_box_down_the_middle()
	{
		let pieces = clip_pieces(
			&unit_box(),
			&Mat4f::identity(),
			&quat_from_array([0.0, 0.0, 0.0, 1.0]),
			Vec3f::new(0.0, 0.0, 0.0),
			Vec3f::new(0.0, 1.0, -1.0),
			Vec3f::new(0.0, 1.0, 1.0),
			Vec3f::new(0.0, 1.0, 0.0),
		)
		.unwrap();

		let kept = Brush::from_planes(&pieces.kept);
		let split = Brush::from_planes(&pieces.split);

		assert!(kept.is_valid() && split.is_valid());
		assert!((kept.bounds_max.x - kept.bounds_min.x - 1.0).abs() < 1e-4);
		assert!((split.bounds_max.x - split.bounds_min.x - 1.0).abs() < 1e-4);
		assert_eq!(pieces.split_position.to_array(), [0.0, 0.0, 0.0]);
	}

	#[test]
	fn a_degenerate_cut_gives_nothing()
	{
		let pieces = clip_pieces(
			&unit_box(),
			&Mat4f::identity(),
			&quat_from_array([0.0, 0.0, 0.0, 1.0]),
			Vec3f::new(0.0, 0.0, 0.0),
			Vec3f::new(0.0, 1.0, 0.0),
			Vec3f::new(0.0, 1.0, 0.0),
			Vec3f::new(0.0, 1.0, 0.0),
		);

		assert!(pieces.is_none());
	}

	#[test]
	fn a_world_box_is_centred_on_its_position()
	{
		let (planes, position) = world_box(Vec3f::new(0.0, 0.0, 0.0), Vec3f::new(2.0, 4.0, 6.0));
		let brush = Brush::from_planes(&planes);

		assert_eq!(position.to_array(), [1.0, 2.0, 3.0]);
		assert!((brush.bounds_max.y - 2.0).abs() < 1e-5);
	}

	#[test]
	fn a_default_box_is_saved_as_a_box_without_textures()
	{
		let shape = save_shape(&unit_box());

		assert_eq!(
			shape,
			SaveShape {
				as_box: true,
				textures: false
			}
		);

		let shifted = edit_face_texture(
			&unit_box(),
			Vec3f::new(1.0, 0.0, 0.0),
			FaceTextureEdit::Shift,
			[1.0, 0.0],
		)
		.unwrap();

		assert!(!save_shape(&shifted).as_box);
		assert!(save_shape(&shifted).textures);
	}

	#[test]
	fn planes_match_only_when_normals_and_distances_do()
	{
		let planes = unit_box();
		let mut other = planes.clone();

		assert!(same_planes(&planes, &other));

		other[0].distance += 0.1;

		assert!(!same_planes(&planes, &other));
	}
}
