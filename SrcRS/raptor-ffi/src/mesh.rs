use std::mem::{offset_of, size_of};
use std::panic::catch_unwind;

use raptor_mesh::{CubeOptions, FaceOptions, Mesh};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxFaceOptions {
	pub scale: f32,
	pub uv_min: [f32; 2],
	pub uv_max: [f32; 2],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxCubeOptions {
	pub left: RxFaceOptions,
	pub right: RxFaceOptions,
	pub top: RxFaceOptions,
	pub bottom: RxFaceOptions,
	pub front: RxFaceOptions,
	pub back: RxFaceOptions,
	pub align_uvs: u8,
}

const _: () = {
	assert!(size_of::<RxFaceOptions>() == 20);
	assert!(offset_of!(RxCubeOptions, back) == 100);
	assert!(offset_of!(RxCubeOptions, align_uvs) == 120);
};

pub struct RxMesh(Mesh);

fn face(options: &RxFaceOptions) -> FaceOptions {
	FaceOptions {
		scale: options.scale,
		uv_min: options.uv_min,
		uv_max: options.uv_max,
	}
}

fn boxed(build: impl FnOnce() -> Mesh + std::panic::UnwindSafe) -> *mut RxMesh {
	catch_unwind(|| Box::into_raw(Box::new(RxMesh(build())))).unwrap_or(std::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_mesh_icosphere(resolution: i32) -> *mut RxMesh {
	boxed(|| raptor_mesh::icosphere(resolution.max(0) as u32))
}

/// # Safety
///
/// `options` must point to a valid `RxCubeOptions`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_cube(options: *const RxCubeOptions) -> *mut RxMesh {
	// SAFETY: guaranteed by the caller.
	let options = unsafe { *options };

	boxed(|| {
		raptor_mesh::cube(&CubeOptions {
			left: face(&options.left),
			right: face(&options.right),
			top: face(&options.top),
			bottom: face(&options.bottom),
			front: face(&options.front),
			back: face(&options.back),
			align_uvs: options.align_uvs != 0,
		})
	})
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_mesh_wireframe_box() -> *mut RxMesh {
	boxed(raptor_mesh::wireframe_box)
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_mesh_line() -> *mut RxMesh {
	boxed(raptor_mesh::line)
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_mesh_quad(scale_x: f32, scale_y: f32) -> *mut RxMesh {
	boxed(move || raptor_mesh::quad(scale_x, scale_y))
}

fn slice_parts<T>(items: &[T], count: &mut usize) -> *const T {
	*count = items.len();
	if items.is_empty() {
		std::ptr::null()
	} else {
		items.as_ptr()
	}
}

/// # Safety
///
/// `mesh` must come from an `rx_mesh_*` constructor and not have been freed. `count` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_positions(mesh: *const RxMesh, count: *mut usize) -> *const f32 {
	// SAFETY: guaranteed by the caller.
	let (mesh, count) = unsafe { (&*mesh, &mut *count) };
	slice_parts(&mesh.0.positions, count).cast()
}

/// # Safety
///
/// As for `rx_mesh_positions`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_normals(mesh: *const RxMesh, count: *mut usize) -> *const f32 {
	// SAFETY: guaranteed by the caller.
	let (mesh, count) = unsafe { (&*mesh, &mut *count) };
	slice_parts(&mesh.0.normals, count).cast()
}

/// # Safety
///
/// As for `rx_mesh_positions`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_tangents(mesh: *const RxMesh, count: *mut usize) -> *const f32 {
	// SAFETY: guaranteed by the caller.
	let (mesh, count) = unsafe { (&*mesh, &mut *count) };
	slice_parts(&mesh.0.tangents, count).cast()
}

/// # Safety
///
/// As for `rx_mesh_positions`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_texcoords(mesh: *const RxMesh, count: *mut usize) -> *const f32 {
	// SAFETY: guaranteed by the caller.
	let (mesh, count) = unsafe { (&*mesh, &mut *count) };
	slice_parts(&mesh.0.texcoords, count).cast()
}

/// # Safety
///
/// As for `rx_mesh_positions`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_indices(mesh: *const RxMesh, count: *mut usize) -> *const u32 {
	// SAFETY: guaranteed by the caller.
	let (mesh, count) = unsafe { (&*mesh, &mut *count) };
	slice_parts(&mesh.0.indices, count)
}

/// # Safety
///
/// `mesh` must be null or come from an `rx_mesh_*` constructor, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_mesh_free(mesh: *mut RxMesh) {
	if !mesh.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(mesh) });
	}
}
