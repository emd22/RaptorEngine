use std::slice;

use raptor_gpu::VertexType;
use raptor_render::mesh_pack::{Attributes, Packed, pack_vertices, recalculate_normals};

pub type RxPackedVertices = Packed;

#[repr(C)]
pub struct RxPackInput
{
	pub positions: *const f32,
	pub position_floats: usize,
	pub normals: *const f32,
	pub normal_floats: usize,
	pub uvs: *const f32,
	pub uv_floats: usize,
	pub tangents: *const f32,
	pub tangent_floats: usize,
	pub tangent_stride: usize,
	pub handedness: f32,
	pub bone_weights: *const f32,
	pub bone_weight_floats: usize,
	pub bone_ids: *const u32,
	pub bone_id_values: usize,
	pub negative_x: bool,
	pub mirror_basis: bool,
}

unsafe fn floats<'a>(data: *const f32, count: usize) -> &'a [f32]
{
	if data.is_null() || count == 0 {
		&[]
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { slice::from_raw_parts(data, count) }
	}
}

/// # Safety
///
/// Every pointer in `input` must be null or valid for its count.
pub(crate) unsafe fn attributes_of(input: &RxPackInput) -> Attributes<'_>
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		Attributes {
			positions: floats(input.positions, input.position_floats),
			normals: floats(input.normals, input.normal_floats),
			uvs: floats(input.uvs, input.uv_floats),
			tangents: floats(input.tangents, input.tangent_floats),
			tangent_stride: input.tangent_stride,
			handedness: input.handedness,
			bone_weights: floats(input.bone_weights, input.bone_weight_floats),
			bone_ids: if input.bone_ids.is_null() || input.bone_id_values == 0 {
				&[]
			} else {
				slice::from_raw_parts(input.bone_ids, input.bone_id_values)
			},
			negative_x: input.negative_x,
			mirror_basis: input.mirror_basis,
		}
	}
}

/// Packs vertex attributes into the vertex format they call for. Returns null if the attributes
/// are not valid.
///
/// # Safety
///
/// Every pointer in `input` must be null or valid for its count.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_pack_vertices(input: *const RxPackInput) -> *mut RxPackedVertices
{
	// SAFETY: guaranteed by the caller.
	let input = unsafe { &*input };

	// SAFETY: guaranteed by the caller.
	let attributes = unsafe { attributes_of(input) };

	pack_vertices(&attributes).map_or(std::ptr::null_mut(), |packed| {
		Box::into_raw(Box::new(packed))
	})
}

/// # Safety
///
/// `packed` must be null or come from `rx_mesh_pack_vertices` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_packed_free(packed: *mut RxPackedVertices)
{
	if !packed.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(packed) });
	}
}

/// # Safety
///
/// `packed` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_packed_data(packed: *const RxPackedVertices) -> *const u8
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*packed }.bytes.as_ptr()
}

/// # Safety
///
/// `packed` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_packed_count(packed: *const RxPackedVertices) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*packed }.count as u32
}

/// The vertex type, then bits for normals (1), uvs (2) and tangents (4) the attributes had.
///
/// # Safety
///
/// `packed` must be live and `out_flags` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_packed_info(
	packed: *const RxPackedVertices,
	out_flags: *mut u32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	let packed = unsafe { &*packed };

	// SAFETY: guaranteed by the caller.
	unsafe {
		*out_flags = u32::from(packed.has_normals)
			| u32::from(packed.has_uvs) << 1
			| u32::from(packed.has_tangents) << 2;
	}

	match packed.vertex_type {
		VertexType::Slim => 0,
		VertexType::Default => 1,
		VertexType::Skinned => 2,
	}
}

/// Recomputes the normals of vertices of `stride` bytes from the triangles `indices` makes. Returns
/// false if the vertices or indices are not valid.
///
/// # Safety
///
/// `vertices` must be valid for `vertex_count * stride` bytes and `indices` for `index_count`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_recalculate_normals(
	vertices: *mut u8,
	vertex_count: usize,
	stride: usize,
	indices: *const u32,
	index_count: usize,
) -> bool
{
	if vertices.is_null() || indices.is_null() {
		return false;
	}

	// SAFETY: guaranteed by the caller.
	let (vertices, indices) = unsafe {
		(
			slice::from_raw_parts_mut(vertices, vertex_count * stride),
			slice::from_raw_parts(indices, index_count),
		)
	};

	recalculate_normals(vertices, stride, indices)
}
