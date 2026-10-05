use std::ffi::{CStr, CString, c_char};
use std::panic::{AssertUnwindSafe, catch_unwind};

use raptor_level::{Collider, ObjectDef, WorldFile};

use crate::RxHost;
use crate::config::CHost;

#[repr(C)]
pub struct RxWorldCollider
{
	pub name: *const c_char,
	pub name_length: usize,
	pub position: [f32; 3],
	pub rotation: [f32; 4],
	pub dynamic: u32,
	pub has_box: u32,
	pub box_size: [f32; 3],
}

#[repr(C)]
pub struct RxWorldObject
{
	pub name: *const c_char,
	pub name_length: usize,
	pub mesh: *const c_char,
	pub collider: *const c_char,
	pub layer: i64,
	pub has_shadows: u32,
	pub shadows: u32,
	pub has_position: u32,
	pub has_rotation: u32,
	pub has_scale: u32,
	pub has_layer: u32,
	pub unlit: u32,
	pub no_cull: u32,
	pub scale: f32,
	pub position: [f32; 3],
	pub rotation: [f32; 4],
}

pub struct RxWorldFile
{
	has_errors: bool,
	has_meta: bool,
	name: Option<CString>,
	colliders: Vec<RxWorldCollider>,
	objects: Vec<RxWorldObject>,
	_text: Vec<CString>,
}

fn c_text(text: &str, store: &mut Vec<CString>) -> *const c_char
{
	let text = CString::new(text.replace('\0', "")).unwrap_or_default();
	let pointer = text.as_ptr();

	store.push(text);

	pointer
}

fn collider_to_c(collider: &Collider, store: &mut Vec<CString>) -> RxWorldCollider
{
	let name = c_text(&collider.name, store);

	RxWorldCollider {
		name,
		name_length: collider.name.len(),
		position: collider.position,
		rotation: collider.rotation,
		dynamic: u32::from(collider.dynamic),
		has_box: u32::from(collider.box_size.is_some()),
		box_size: collider.box_size.unwrap_or_default(),
	}
}

fn object_to_c(object: &ObjectDef, store: &mut Vec<CString>) -> RxWorldObject
{
	let name = c_text(&object.name, store);

	RxWorldObject {
		name,
		name_length: object.name.len(),
		mesh: object
			.mesh
			.as_deref()
			.map_or(std::ptr::null(), |mesh| c_text(mesh, store)),
		collider: object
			.collider
			.as_deref()
			.map_or(std::ptr::null(), |collider| c_text(collider, store)),
		layer: object.layer.unwrap_or_default(),
		has_shadows: u32::from(object.shadows.is_some()),
		shadows: u32::from(object.shadows.unwrap_or_default()),
		has_position: u32::from(object.position.is_some()),
		has_rotation: u32::from(object.rotation.is_some()),
		has_scale: u32::from(object.scale.is_some()),
		has_layer: u32::from(object.layer.is_some()),
		unlit: u32::from(object.unlit),
		no_cull: u32::from(object.no_cull),
		scale: object.scale.unwrap_or_default(),
		position: object.position.unwrap_or_default(),
		rotation: object.rotation.unwrap_or_default(),
	}
}

/// Parses the text of a world file. `prelude_path` is the file of constants it may refer to, or
/// null.
///
/// # Safety
///
/// `data` must point at `length` readable bytes, `prelude_path` must be null or NUL-terminated,
/// and `host` must be valid. The result lasts until `rx_world_file_free`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_parse(
	data: *const u8,
	length: usize,
	prelude_path: *const c_char,
	host: *const RxHost,
) -> *mut RxWorldFile
{
	catch_unwind(AssertUnwindSafe(|| {
		let data = if data.is_null() {
			&[][..]
		}
		else {
			// SAFETY: guaranteed by the caller.
			unsafe { std::slice::from_raw_parts(data, length) }
		};

		let prelude = (!prelude_path.is_null()).then(|| {
			// SAFETY: NUL-terminated by the caller.
			unsafe { CStr::from_ptr(prelude_path) }.to_bytes()
		});

		// SAFETY: guaranteed by the caller.
		let mut host = CHost(unsafe { &*host });

		let world = WorldFile::parse(data, prelude, &mut host);

		let mut text = Vec::new();

		let colliders = world
			.colliders
			.iter()
			.map(|collider| collider_to_c(collider, &mut text))
			.collect();

		let objects = world
			.objects
			.iter()
			.map(|object| object_to_c(object, &mut text))
			.collect();

		Box::into_raw(Box::new(RxWorldFile {
			has_errors: world.has_errors,
			has_meta: world.has_meta,
			name: world
				.name
				.and_then(|name| CString::new(name.replace('\0', "")).ok()),
			colliders,
			objects,
			_text: text,
		}))
	}))
	.unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `world` must be null or come from `rx_world_file_parse`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_free(world: *mut RxWorldFile)
{
	if !world.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(world) });
	}
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_has_errors(world: *const RxWorldFile) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*world }.has_errors)
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_has_meta(world: *const RxWorldFile) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*world }.has_meta)
}

/// The `name` in the file's `meta`, or null if it has none.
///
/// # Safety
///
/// `world` must be live. The name lasts as long as the world file.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_name(world: *const RxWorldFile) -> *const c_char
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }
		.name
		.as_ref()
		.map_or(std::ptr::null(), |name| name.as_ptr())
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_collider_count(world: *const RxWorldFile) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }.colliders.len() as u32
}

/// # Safety
///
/// `world` must be live. The collider lasts as long as the world file.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_collider(
	world: *const RxWorldFile,
	index: u32,
) -> *const RxWorldCollider
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }
		.colliders
		.get(index as usize)
		.map_or(std::ptr::null(), std::ptr::from_ref)
}

/// # Safety
///
/// `world` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_object_count(world: *const RxWorldFile) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }.objects.len() as u32
}

/// # Safety
///
/// `world` must be live. The object lasts as long as the world file.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_file_object(
	world: *const RxWorldFile,
	index: u32,
) -> *const RxWorldObject
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*world }
		.objects
		.get(index as usize)
		.map_or(std::ptr::null(), std::ptr::from_ref)
}

const _: () = {
	use std::mem::size_of;

	assert!(size_of::<RxWorldCollider>() == 64);
	assert!(size_of::<RxWorldObject>() == 104);
};
