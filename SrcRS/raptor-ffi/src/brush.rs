use std::mem::{offset_of, size_of};
use std::panic::{AssertUnwindSafe, catch_unwind};

use raptor_brush::{Brush, Face, FaceTexture, Plane};
use raptor_math::Vec3f;

use crate::mesh::RxMesh;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BrushPlane {
	pub normal: [f32; 3],
	pub distance: f32,
	pub offset: [f32; 2],
	pub scale: [f32; 2],
	pub rotation: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BrushFace {
	pub plane_index: u32,
	pub vertices: *const f32,
	pub vertex_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BrushView {
	pub planes: *const BrushPlane,
	pub plane_count: usize,
	pub faces: *const BrushFace,
	pub face_count: usize,
	pub vertices: *const f32,
	pub vertex_count: usize,
	pub bounds_min: [f32; 3],
	pub bounds_max: [f32; 3],
}

const _: () = {
	assert!(size_of::<BrushPlane>() == 36);
	assert!(offset_of!(BrushPlane, distance) == 12);
	assert!(offset_of!(BrushPlane, rotation) == 32);
	assert!(size_of::<BrushFace>() == 24);
	assert!(size_of::<BrushView>() == 72);
	assert!(offset_of!(BrushView, bounds_min) == 48);
};

fn plane_from_c(plane: &BrushPlane) -> Plane {
	Plane {
		normal: Vec3f::from_array(plane.normal),
		distance: plane.distance,
		texture: FaceTexture {
			offset: plane.offset,
			scale: plane.scale,
			rotation: plane.rotation,
		},
	}
}

fn plane_to_c(plane: &Plane) -> BrushPlane {
	BrushPlane {
		normal: plane.normal.to_array(),
		distance: plane.distance,
		offset: plane.texture.offset,
		scale: plane.texture.scale,
		rotation: plane.texture.rotation,
	}
}

unsafe fn slice<'a, T>(pointer: *const T, count: usize) -> &'a [T] {
	if pointer.is_null() || count == 0 {
		&[]
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(pointer, count) }
	}
}

fn unpack(floats: &[f32]) -> Vec<Vec3f> {
	floats
		.as_chunks::<3>()
		.0
		.iter()
		.map(|&chunk| Vec3f::from_array(chunk))
		.collect()
}

/// # Safety
///
/// `view` and everything it points to must be valid.
unsafe fn brush_from_view(view: &BrushView) -> Brush {
	// SAFETY: guaranteed by the caller.
	unsafe {
		Brush {
			planes: slice(view.planes, view.plane_count)
				.iter()
				.map(plane_from_c)
				.collect(),
			faces: slice(view.faces, view.face_count)
				.iter()
				.map(|face| Face {
					plane_index: face.plane_index,
					vertices: unpack(slice(face.vertices, face.vertex_count * 3)),
				})
				.collect(),
			vertices: unpack(slice(view.vertices, view.vertex_count * 3)),
			bounds_min: Vec3f::from_array(view.bounds_min),
			bounds_max: Vec3f::from_array(view.bounds_max),
		}
	}
}

pub struct BrushResult {
	ok: bool,
	view: BrushView,
	_planes: Vec<BrushPlane>,
	_faces: Vec<BrushFace>,
	_face_vertices: Vec<Vec<f32>>,
	_vertices: Vec<f32>,
}

fn pack(vertices: &[Vec3f]) -> Vec<f32> {
	vertices.iter().flat_map(|v| v.to_array()).collect()
}

fn into_result(brush: &Brush, ok: bool) -> *mut BrushResult {
	let planes: Vec<BrushPlane> = brush.planes.iter().map(plane_to_c).collect();
	let face_vertices: Vec<Vec<f32>> = brush.faces.iter().map(|f| pack(&f.vertices)).collect();
	let faces: Vec<BrushFace> = brush
		.faces
		.iter()
		.zip(&face_vertices)
		.map(|(face, packed)| BrushFace {
			plane_index: face.plane_index,
			vertices: packed.as_ptr(),
			vertex_count: face.vertices.len(),
		})
		.collect();
	let vertices = pack(&brush.vertices);

	let view = BrushView {
		planes: planes.as_ptr(),
		plane_count: planes.len(),
		faces: faces.as_ptr(),
		face_count: faces.len(),
		vertices: vertices.as_ptr(),
		vertex_count: brush.vertices.len(),
		bounds_min: brush.bounds_min.to_array(),
		bounds_max: brush.bounds_max.to_array(),
	};

	Box::into_raw(Box::new(BrushResult {
		ok,
		view,
		_planes: planes,
		_faces: faces,
		_face_vertices: face_vertices,
		_vertices: vertices,
	}))
}

fn guarded<T>(build: impl FnOnce() -> *mut T) -> *mut T {
	catch_unwind(AssertUnwindSafe(build)).unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `min` and `max` must point to three floats each.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_from_box(min: *const f32, max: *const f32) -> *mut BrushResult {
	// SAFETY: guaranteed by the caller.
	let (min, max) = unsafe {
		(
			Vec3f::new(*min, *min.add(1), *min.add(2)),
			Vec3f::new(*max, *max.add(1), *max.add(2)),
		)
	};

	guarded(|| {
		let brush = Brush::from_box(min, max);
		into_result(&brush, brush.is_valid())
	})
}

/// # Safety
///
/// `planes` must point to `count` valid planes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_rebuild(
	planes: *const BrushPlane,
	count: usize,
) -> *mut BrushResult {
	// SAFETY: guaranteed by the caller.
	let planes: Vec<Plane> = unsafe { slice(planes, count) }
		.iter()
		.map(plane_from_c)
		.collect();

	guarded(|| {
		let mut brush = Brush {
			planes,
			..Brush::default()
		};
		let ok = brush.rebuild();
		into_result(&brush, ok)
	})
}

/// # Safety
///
/// `result` must come from a `rx_brush_*` constructor and not have been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_result_ok(result: *const BrushResult) -> i32 {
	// SAFETY: guaranteed by the caller.
	i32::from(unsafe { &*result }.ok)
}

/// # Safety
///
/// `result` must come from a `rx_brush_*` constructor and not have been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_result_view(result: *const BrushResult) -> *const BrushView {
	// SAFETY: guaranteed by the caller.
	&unsafe { &*result }.view
}

/// # Safety
///
/// `result` must be null or come from a `rx_brush_*` constructor, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_result_free(result: *mut BrushResult) {
	if !result.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(result) });
	}
}

fn vec3(pointer: *const f32) -> Vec3f {
	// SAFETY: the callers of the public functions promise three readable floats.
	unsafe { Vec3f::new(*pointer, *pointer.add(1), *pointer.add(2)) }
}

/// # Safety
///
/// `view` must be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_is_box(view: *const BrushView) -> i32 {
	// SAFETY: guaranteed by the caller.
	i32::from(unsafe { brush_from_view(&*view) }.is_box())
}

/// # Safety
///
/// `view` must be valid and `point` must point to three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_contains_point(
	view: *const BrushView,
	point: *const f32,
	tolerance: f32,
) -> i32 {
	// SAFETY: guaranteed by the caller.
	i32::from(unsafe { brush_from_view(&*view) }.contains_point(vec3(point), tolerance))
}

/// # Safety
///
/// `view` must be valid and `normal` must point to three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_find_plane(view: *const BrushView, normal: *const f32) -> i32 {
	// SAFETY: guaranteed by the caller.
	unsafe { brush_from_view(&*view) }.find_plane(vec3(normal))
}

/// # Safety
///
/// `view` must be valid and `direction` must point to three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_support(view: *const BrushView, direction: *const f32) -> f32 {
	// SAFETY: guaranteed by the caller.
	unsafe { brush_from_view(&*view) }.support(vec3(direction))
}

/// # Safety
///
/// `view` must be valid and `out` must point to three writable floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_face_center(
	view: *const BrushView,
	plane_index: u32,
	out: *mut f32,
) {
	// SAFETY: guaranteed by the caller.
	let center = unsafe { brush_from_view(&*view) }
		.face_center(plane_index)
		.to_array();
	// SAFETY: guaranteed by the caller.
	unsafe { std::ptr::copy_nonoverlapping(center.as_ptr(), out, 3) };
}

/// # Safety
///
/// `view` must be valid, `origin` and `direction` must point to three floats and the outputs must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_raycast(
	view: *const BrushView,
	origin: *const f32,
	direction: *const f32,
	out_distance: *mut f32,
	out_plane_index: *mut u32,
) -> i32 {
	// SAFETY: guaranteed by the caller.
	let hit = unsafe { brush_from_view(&*view) }.raycast(vec3(origin), vec3(direction));

	match hit {
		Some((distance, plane)) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*out_distance = distance;
				*out_plane_index = plane;
			}
			1
		}
		None => 0,
	}
}

/// # Safety
///
/// `view` must be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_has_default_textures(view: *const BrushView) -> i32 {
	// SAFETY: guaranteed by the caller.
	i32::from(unsafe { brush_from_view(&*view) }.has_default_textures())
}

/// # Safety
///
/// `view` must be valid and `out_offset` must point to two writable floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_default_texture_offset(
	view: *const BrushView,
	plane_index: u32,
	out_offset: *mut f32,
) {
	// SAFETY: guaranteed by the caller.
	let mut brush = unsafe { brush_from_view(&*view) };

	let offset = if (plane_index as usize) < brush.planes.len() {
		brush.reset_face_texture(plane_index as usize);
		brush.planes[plane_index as usize].texture.offset
	} else {
		[0.0; 2]
	};

	// SAFETY: guaranteed by the caller.
	unsafe { std::ptr::copy_nonoverlapping(offset.as_ptr(), out_offset, 2) };
}

/// # Safety
///
/// `normal` and `origin` must point to three floats and `out_offset` to two writable floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_world_aligned_offset(
	normal: *const f32,
	origin: *const f32,
	out_offset: *mut f32,
) {
	let mut brush = Brush {
		planes: vec![Plane::new(vec3(normal), 0.0)],
		..Brush::default()
	};
	brush.align_textures_to_world(vec3(origin));
	// SAFETY: guaranteed by the caller.
	unsafe {
		std::ptr::copy_nonoverlapping(brush.planes[0].texture.offset.as_ptr(), out_offset, 2)
	};
}

pub struct BrushSplit {
	back: Vec<BrushPlane>,
	front: Vec<BrushPlane>,
}

/// # Safety
///
/// `view` must be valid and `normal` and `origin` must point to three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_split(
	view: *const BrushView,
	normal: *const f32,
	distance: f32,
	origin: *const f32,
) -> *mut BrushSplit {
	// SAFETY: guaranteed by the caller.
	let brush = unsafe { brush_from_view(&*view) };
	let (normal, origin) = (vec3(normal), vec3(origin));

	guarded(|| match brush.split(normal, distance, origin) {
		Some((back, front)) => Box::into_raw(Box::new(BrushSplit {
			back: back.iter().map(plane_to_c).collect(),
			front: front.iter().map(plane_to_c).collect(),
		})),
		None => std::ptr::null_mut(),
	})
}

/// # Safety
///
/// `split` must come from `rx_brush_split` and not have been freed. `count` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_split_planes(
	split: *const BrushSplit,
	front: i32,
	count: *mut usize,
) -> *const BrushPlane {
	// SAFETY: guaranteed by the caller.
	let (split, count) = unsafe { (&*split, &mut *count) };
	let planes = if front != 0 {
		&split.front
	} else {
		&split.back
	};
	*count = planes.len();
	planes.as_ptr()
}

/// # Safety
///
/// `split` must be null or come from `rx_brush_split`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_split_free(split: *mut BrushSplit) {
	if !split.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(split) });
	}
}

/// # Safety
///
/// `view` must be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_brush_generate_mesh(view: *const BrushView) -> *mut RxMesh {
	// SAFETY: guaranteed by the caller.
	let brush = unsafe { brush_from_view(&*view) };

	guarded(|| Box::into_raw(Box::new(RxMesh(brush.generate_mesh()))))
}
