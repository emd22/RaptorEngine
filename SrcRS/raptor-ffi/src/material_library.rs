use std::ffi::{CStr, CString, c_char};
use std::mem::size_of;
use std::panic::{AssertUnwindSafe, catch_unwind};

use raptor_level::{MaterialList, MaterialRegistry};

use crate::RxHost;
use crate::config::CHost;

#[repr(C)]
pub struct RxMaterialDef
{
	pub name: *const c_char,
	pub diffuse: *const c_char,
	pub normal: *const c_char,
	pub orm: *const c_char,
}

const _: () = assert!(size_of::<RxMaterialDef>() == 32);

pub struct RxMaterialLibrary
{
	defs: Vec<RxMaterialDef>,
	registry: MaterialRegistry,
	_text: Vec<CString>,
}

fn c_text(text: &str, store: &mut Vec<CString>) -> *const c_char
{
	let text = CString::new(text.replace('\0', "")).unwrap_or_default();
	let pointer = text.as_ptr();

	store.push(text);

	pointer
}

/// The result lasts until `rx_material_library_free`.
#[unsafe(no_mangle)]
pub extern "C" fn rx_material_library_new() -> *mut RxMaterialLibrary
{
	Box::into_raw(Box::new(RxMaterialLibrary {
		defs: Vec::new(),
		registry: MaterialRegistry::new(),
		_text: Vec::new(),
	}))
}

/// # Safety
///
/// `library` must be null or come from `rx_material_library_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_free(library: *mut RxMaterialLibrary)
{
	if !library.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(library) });
	}
}

/// Parses the text of a material list, replacing what the library held. Returns 1 if the file has a
/// `list`, otherwise 0.
///
/// # Safety
///
/// `library` must be live, `data` must point at `length` readable bytes, `prelude_path` must be null
/// or NUL-terminated, and `host` must be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_parse(
	library: *mut RxMaterialLibrary,
	data: *const u8,
	length: usize,
	prelude_path: *const c_char,
	host: *const RxHost,
) -> u8
{
	catch_unwind(AssertUnwindSafe(|| {
		// SAFETY: guaranteed by the caller.
		let library = unsafe { &mut *library };

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

		let list = MaterialList::parse(data, prelude, &mut host);

		library.registry.clear();
		library.defs.clear();
		library._text.clear();

		for def in &list.entries {
			library.defs.push(RxMaterialDef {
				name: c_text(&def.name, &mut library._text),
				diffuse: c_text(&def.diffuse, &mut library._text),
				normal: c_text(&def.normal, &mut library._text),
				orm: c_text(&def.orm, &mut library._text),
			});
		}

		u8::from(list.has_list)
	}))
	.unwrap_or(0)
}

/// # Safety
///
/// `library` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_def_count(library: *const RxMaterialLibrary) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*library }.defs.len() as u32
}

/// # Safety
///
/// `library` must be live. The definition lasts until the library is parsed again or freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_def(
	library: *const RxMaterialLibrary,
	index: u32,
) -> *const RxMaterialDef
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*library }
		.defs
		.get(index as usize)
		.map_or(std::ptr::null(), std::ptr::from_ref)
}

/// Records the material created for the next list index.
///
/// # Safety
///
/// `library` must be live and `name` NUL-terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_register(
	library: *mut RxMaterialLibrary,
	name: *const c_char,
	material: u32,
)
{
	// SAFETY: guaranteed by the caller.
	let (library, name) = unsafe { (&mut *library, CStr::from_ptr(name)) };

	library.registry.push(&name.to_string_lossy(), material);
}

/// # Safety
///
/// `library` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_count(library: *const RxMaterialLibrary) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*library }.registry.len() as u32
}

/// The name of a material, or "unknown" past the end. Valid until the library next changes.
///
/// # Safety
///
/// `library` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_name(
	library: *const RxMaterialLibrary,
	index: u32,
) -> *const c_char
{
	// SAFETY: guaranteed by the caller.
	let library = unsafe { &*library };

	match library.defs.get(index as usize) {
		Some(def) if (index as usize) < library.registry.len() => def.name,
		_ => c"unknown".as_ptr(),
	}
}

/// # Safety
///
/// `library` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_material(
	library: *const RxMaterialLibrary,
	index: i32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*library }.registry.material(index)
}

/// The list index of a material, or -1.
///
/// # Safety
///
/// `library` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_material_library_find(
	library: *const RxMaterialLibrary,
	material: u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*library }.registry.find(material)
}
