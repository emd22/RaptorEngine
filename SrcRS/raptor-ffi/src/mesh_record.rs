use std::slice;

use raptor_gpu::VertexType;
use raptor_render::mesh_record::{MeshRecord, NormalsStatus};

use crate::mesh_pack::{RxPackInput, attributes_of};

pub type RxMeshRecord = MeshRecord;

fn vertex_type(value: u32) -> VertexType
{
	match value {
		0 => VertexType::Slim,
		2 => VertexType::Skinned,
		_ => VertexType::Default,
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_mesh_record_new() -> *mut RxMeshRecord
{
	Box::into_raw(Box::default())
}

/// # Safety
///
/// `mesh` must be null or come from `rx_mesh_record_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_free(mesh: *mut RxMeshRecord)
{
	if !mesh.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(mesh) });
	}
}

/// Packs vertex attributes into the record, returning whether they were valid.
///
/// # Safety
///
/// `mesh` must be live and every pointer in `input` null or valid for its count.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_pack(
	mesh: *mut RxMeshRecord,
	input: *const RxPackInput,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let attributes = attributes_of(&*input);

		u8::from((*mesh).pack(&attributes))
	}
}

/// # Safety
///
/// `mesh` must be live and `bytes` hold `size` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_set_vertices(
	mesh: *mut RxMeshRecord,
	vertex_type_value: u32,
	bytes: *const u8,
	size: usize,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *mesh }.set_vertices(vertex_type(vertex_type_value), unsafe {
		slice::from_raw_parts(bytes, size)
	});
}

/// # Safety
///
/// `mesh` must be live and `indices` hold `count` values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_set_indices(
	mesh: *mut RxMeshRecord,
	indices: *const u32,
	count: usize,
)
{
	let indices = if indices.is_null() || count == 0 {
		&[]
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { slice::from_raw_parts(indices, count) }
	};

	// SAFETY: guaranteed by the caller.
	unsafe { &mut *mesh }.set_indices(indices);
}

/// Status of working out normals: 0 present, 1 recalculated, 2 no indices, 3 no vertices, 4 the
/// indices do not fit.
///
/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_ensure_normals(mesh: *mut RxMeshRecord) -> u32
{
	// SAFETY: guaranteed by the caller.
	match unsafe { &mut *mesh }.ensure_normals() {
		NormalsStatus::Present => 0,
		NormalsStatus::Recalculated => 1,
		NormalsStatus::MissingIndices => 2,
		NormalsStatus::MissingVertices => 3,
		NormalsStatus::IndicesDoNotFit => 4,
	}
}

/// # Safety
///
/// `mesh` must be live and `out_size` writable. The pointer lasts until the vertices change.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_vertex_bytes(
	mesh: *const RxMeshRecord,
	out_size: *mut usize,
) -> *const u8
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let bytes = (*mesh).vertex_bytes();

		out_size.write(bytes.len());

		bytes.as_ptr()
	}
}

/// # Safety
///
/// `mesh` must be live and `out_count` writable. The pointer lasts until the indices change.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_indices(
	mesh: *const RxMeshRecord,
	out_count: *mut usize,
) -> *const u32
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let indices = (*mesh).indices();

		out_count.write(indices.len());

		indices.as_ptr()
	}
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_vertex_count(mesh: *const RxMeshRecord) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*mesh }.vertex_count() as u32
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_vertex_type(mesh: *const RxMeshRecord) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*mesh }.vertex_type() as u32
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_is_skinned(mesh: *const RxMeshRecord) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*mesh }.is_skinned())
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_is_ready(mesh: *const RxMeshRecord) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*mesh }.is_ready())
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_set_ready(mesh: *const RxMeshRecord, ready: u8)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*mesh }.set_ready(ready != 0);
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_is_reference(mesh: *const RxMeshRecord) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*mesh }.is_reference)
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_keeps_in_memory(mesh: *const RxMeshRecord) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*mesh }.keep_in_memory)
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_set_keep_in_memory(mesh: *mut RxMeshRecord, value: u8)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *mesh }.keep_in_memory = value != 0;
}

/// Writes every vertex position as x y z into `out`, which must hold three floats per vertex.
///
/// # Safety
///
/// `mesh` must be live and `out` writable for `rx_mesh_record_vertex_count() * 3` floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_positions(mesh: *const RxMeshRecord, out: *mut f32)
{
	// SAFETY: guaranteed by the caller.
	let positions = unsafe { &*mesh }.positions();

	// SAFETY: guaranteed by the caller.
	unsafe { slice::from_raw_parts_mut(out, positions.len()) }.copy_from_slice(&positions);
}

/// Writes the box around the vertices, returning zero if there are none.
///
/// # Safety
///
/// `mesh` must be live and the outputs writable for three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_bounds(
	mesh: *const RxMeshRecord,
	out_min: *mut f32,
	out_max: *mut f32,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	let Some((min, max)) = (unsafe { &*mesh }).bounds() else {
		return 0;
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		slice::from_raw_parts_mut(out_min, 3).copy_from_slice(&min);
		slice::from_raw_parts_mut(out_max, 3).copy_from_slice(&max);
	}

	1
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_clear_local(mesh: *mut RxMeshRecord)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *mesh }.clear_local();
}

/// # Safety
///
/// `mesh` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_record_clear_vertices(mesh: *mut RxMeshRecord)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *mesh }.clear_vertices();
}
