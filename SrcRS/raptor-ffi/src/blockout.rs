use raptor_brush::edit::{
	FaceTextureEdit, brush_planes, clip_pieces, edit_face_texture, keep_in_place_offset, move_face,
	quat_from_array, same_planes, save_shape, world_box,
};
use raptor_brush::{Brush, FaceTexture, MAX_PLANES, Plane};
use raptor_math::{Mat4f, Vec3f};
use raptor_world::script_math::{direction_to_world, point_to_world, ray_to_plane};

use crate::brush::{BrushPlane, plane_from_c, plane_to_c};
use crate::level::RxLevelBlock;

const BRUSH_BOX: u32 = 1;
const BRUSH_PLANES: u32 = 2;

pub const SHAPE_BOX: u32 = 1 << 0;
pub const SHAPE_TEXTURES: u32 = 1 << 1;

unsafe fn vec3(values: *const f32) -> Vec3f
{
	// SAFETY: guaranteed by the caller.
	Vec3f::from_array(unsafe { *values.cast::<[f32; 3]>() })
}

unsafe fn planes_in(planes: *const BrushPlane, count: usize) -> Vec<Plane>
{
	if planes.is_null() || count == 0 {
		return Vec::new();
	}

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts(planes, count) }
		.iter()
		.map(plane_from_c)
		.collect()
}

unsafe fn planes_out(planes: &[Plane], out: *mut BrushPlane, capacity: usize) -> usize
{
	if planes.len() > capacity {
		return 0;
	}

	for (index, plane) in planes.iter().enumerate() {
		// SAFETY: `index` is within `capacity`, which the caller guarantees is writable.
		unsafe { out.add(index).write(plane_to_c(plane)) };
	}

	planes.len()
}

/// Where an object has to move for the parts of a brush it keeps to stay put when the centre of
/// the brush moves.
///
/// # Safety
///
/// `rotation` must hold four floats, the others three, and `out` be writable for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blockout_keep_in_place(
	rotation: *const f32,
	old_center: *const f32,
	new_center: *const f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (rotation, old_center, new_center) = unsafe {
		(
			quat_from_array(*rotation.cast::<[f32; 4]>()),
			vec3(old_center),
			vec3(new_center),
		)
	};

	let offset = keep_in_place_offset(&rotation, old_center, new_center);

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts_mut(out, 3) }.copy_from_slice(&offset.to_array());
}

/// Writes the planes of a brush with one face pushed out, returning how many or zero if there is
/// no such face or the brush would not be valid.
///
/// # Safety
///
/// `planes` must hold `count` planes, `normal` three floats, and `out` be writable for `capacity`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blockout_move_face(
	planes: *const BrushPlane,
	count: usize,
	normal: *const f32,
	distance: f32,
	min_thickness: f32,
	out: *mut BrushPlane,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let (planes, normal) = unsafe { (planes_in(planes, count), vec3(normal)) };

	move_face(&planes, normal, distance, min_thickness)
		// SAFETY: guaranteed by the caller.
		.map_or(0, |moved| unsafe { planes_out(&moved, out, capacity) })
}

/// Writes the planes of a brush with the texture on one face changed, returning how many or zero
/// if there is no such face.
///
/// # Safety
///
/// `planes` must hold `count` planes, `normal` three floats, `amount` two, and `out` be writable
/// for `capacity`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blockout_edit_face_texture(
	planes: *const BrushPlane,
	count: usize,
	normal: *const f32,
	edit: u32,
	amount: *const f32,
	out: *mut BrushPlane,
	capacity: usize,
) -> usize
{
	let Some(edit) = FaceTextureEdit::from_u32(edit) else {
		return 0;
	};

	// SAFETY: guaranteed by the caller.
	let (planes, normal, amount) = unsafe {
		(
			planes_in(planes, count),
			vec3(normal),
			*amount.cast::<[f32; 2]>(),
		)
	};

	edit_face_texture(&planes, normal, edit, amount)
		// SAFETY: guaranteed by the caller.
		.map_or(0, |edited| unsafe { planes_out(&edited, out, capacity) })
}

/// Splits a brush along the plane through two points drawn on a face. Returns whether it split,
/// with the plane counts of the two pieces in `out_counts` and where the new piece's object goes.
///
/// # Safety
///
/// `planes` must hold `count` planes, `to_local` sixteen floats, `rotation` four, the points and
/// normal three each, and the outputs be writable: both piece buffers for `capacity`, `out_counts`
/// for two and `out_split_position` for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blockout_clip(
	planes: *const BrushPlane,
	count: usize,
	to_local: *const f32,
	rotation: *const f32,
	position: *const f32,
	point_a: *const f32,
	point_b: *const f32,
	face_normal: *const f32,
	out_kept: *mut BrushPlane,
	out_split: *mut BrushPlane,
	capacity: usize,
	out_counts: *mut usize,
	out_split_position: *mut f32,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	let (planes, to_local, rotation, position, a, b, normal) = unsafe {
		(
			planes_in(planes, count),
			Mat4f::from_rows(&*to_local.cast::<[f32; 16]>()),
			quat_from_array(*rotation.cast::<[f32; 4]>()),
			vec3(position),
			vec3(point_a),
			vec3(point_b),
			vec3(face_normal),
		)
	};

	let Some(pieces) = clip_pieces(&planes, &to_local, &rotation, position, a, b, normal) else {
		return 0;
	};

	if pieces.kept.len() > capacity || pieces.split.len() > capacity {
		return 0;
	}

	// SAFETY: guaranteed by the caller.
	unsafe {
		let kept = planes_out(&pieces.kept, out_kept, capacity);
		let split = planes_out(&pieces.split, out_split, capacity);

		out_counts.write(kept);
		out_counts.add(1).write(split);

		std::slice::from_raw_parts_mut(out_split_position, 3)
			.copy_from_slice(&pieces.split_position.to_array());
	}

	1
}

/// Writes the planes of a box filling `min` to `max` with its textures lined up with the world
/// grid, and the position its object goes at.
///
/// # Safety
///
/// `min` and `max` must hold three floats, `out` be writable for `capacity` and `out_position` for
/// three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blockout_world_box(
	min: *const f32,
	max: *const f32,
	out: *mut BrushPlane,
	capacity: usize,
	out_position: *mut f32,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let (planes, position) = world_box(unsafe { vec3(min) }, unsafe { vec3(max) });

	// SAFETY: guaranteed by the caller.
	unsafe {
		std::slice::from_raw_parts_mut(out_position, 3).copy_from_slice(&position.to_array());
		planes_out(&planes, out, capacity)
	}
}

/// Writes the planes of the brush a level block describes, or zero planes if it describes none.
///
/// # Safety
///
/// `block` must be valid and `out` writable for `capacity`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blockout_make_planes(
	block: *const RxLevelBlock,
	out: *mut BrushPlane,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let block = unsafe { &*block };

	let planes = match block.brush_kind {
		BRUSH_BOX => {
			Brush::from_box(
				Vec3f::from_array(block.box_min),
				Vec3f::from_array(block.box_max),
			)
			.planes
		}
		BRUSH_PLANES if !block.planes.is_null() => {
			let count = (block.plane_count as usize).min(MAX_PLANES);

			// SAFETY: the block holds `plane_count` planes.
			let listed: Vec<Plane> = unsafe { std::slice::from_raw_parts(block.planes, count) }
				.iter()
				.map(|plane| Plane {
					normal: Vec3f::from_array(plane.normal),
					distance: plane.distance,
					texture: if plane.has_texture != 0 {
						FaceTexture {
							offset: plane.offset,
							scale: plane.scale,
							rotation: plane.rotation,
						}
					} else {
						FaceTexture::default()
					},
				})
				.collect();

			brush_planes(&listed, block.has_textures != 0)
		}
		_ => return 0,
	};

	// SAFETY: guaranteed by the caller.
	unsafe { planes_out(&planes, out, capacity) }
}

/// Whether two plane lists have the same normals and distances.
///
/// # Safety
///
/// Each list must hold as many planes as its count says.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blockout_same_planes(
	a: *const BrushPlane,
	a_count: usize,
	b: *const BrushPlane,
	b_count: usize,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	let (a, b) = unsafe { (planes_in(a, a_count), planes_in(b, b_count)) };

	u8::from(same_planes(&a, &b))
}

/// How a blockout is written to a level file: bit 0 set for a plain box, bit 1 set when the faces
/// carry their own textures.
///
/// # Safety
///
/// `planes` must hold `count` planes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_blockout_save_shape(planes: *const BrushPlane, count: usize) -> u32
{
	// SAFETY: guaranteed by the caller.
	let shape = save_shape(&unsafe { planes_in(planes, count) });

	(if shape.as_box { SHAPE_BOX } else { 0 }) | (if shape.textures { SHAPE_TEXTURES } else { 0 })
}

/// # Safety
///
/// `matrix` must hold sixteen floats, `point` three and `out` be writable for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_script_point_to_world(
	matrix: *const f32,
	point: *const f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let world = point_to_world(&Mat4f::from_rows(&*matrix.cast::<[f32; 16]>()), vec3(point));

		std::slice::from_raw_parts_mut(out, 3).copy_from_slice(&world.to_array());
	}
}

/// # Safety
///
/// `matrix` must hold sixteen floats, `direction` three and `out` be writable for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_script_direction_to_world(
	matrix: *const f32,
	direction: *const f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let world = direction_to_world(
			&Mat4f::from_rows(&*matrix.cast::<[f32; 16]>()),
			vec3(direction),
		);

		std::slice::from_raw_parts_mut(out, 3).copy_from_slice(&world.to_array());
	}
}

/// # Safety
///
/// Every vector must hold three floats and `out` be writable for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_script_ray_to_plane(
	origin: *const f32,
	direction: *const f32,
	point: *const f32,
	normal: *const f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let hit = ray_to_plane(vec3(origin), vec3(direction), vec3(point), vec3(normal));

		std::slice::from_raw_parts_mut(out, 3).copy_from_slice(&hit.to_array());
	}
}
