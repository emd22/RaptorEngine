#![allow(clippy::undocumented_unsafe_blocks)]
// It's not really dead code, its just exported with a C API
#![allow(dead_code)]

use std::ffi::{CStr, c_char, c_void};
use std::path::PathBuf;

use raptor_config::model::{Entry, Kind, Primitive};
use raptor_ffi::*;

struct Context
{
	root: PathBuf,
	logged: Vec<(i32, i32, Vec<u8>)>,
}

unsafe extern "C" fn read_include(
	user: *mut c_void,
	path: *const c_char,
	extension: *const c_char,
	data: *mut *mut u8,
	length: *mut usize,
) -> i32
{
	let context = unsafe { &*(user as *const Context) };
	let mut path = unsafe { CStr::from_ptr(path) }
		.to_string_lossy()
		.into_owned();
	let extension = unsafe { CStr::from_ptr(extension) }
		.to_string_lossy()
		.into_owned();

	let basename = path.rsplit('/').find(|c| !c.is_empty()).unwrap_or("");
	if !matches!(basename.rfind('.'), Some(i) if i != 0) {
		path.push_str(&extension);
	}

	match std::fs::read(context.root.join(path)) {
		Ok(bytes) => {
			let boxed = bytes.into_boxed_slice();
			unsafe {
				*length = boxed.len();
				*data = Box::into_raw(boxed) as *mut u8;
			}
			1
		}
		Err(_) => 0,
	}
}

unsafe extern "C" fn release_include(_user: *mut c_void, _data: *mut u8) {}

unsafe extern "C" fn log(
	user: *mut c_void,
	level: i32,
	category: i32,
	message: *const c_char,
	length: usize,
)
{
	let context = unsafe { &mut *(user as *mut Context) };
	let bytes = unsafe { std::slice::from_raw_parts(message as *const u8, length) }.to_vec();
	context.logged.push((level, category, bytes));
}

fn primitive_from_c(p: &ConfigPrimitive) -> Primitive
{
	let kind = match p.kind {
		0 => Kind::None,
		1 => Kind::Int,
		2 => Kind::Float,
		3 => Kind::String,
		_ => Kind::Struct,
	};
	let string_value = if p.string_value.is_null() {
		None
	} else {
		let bytes =
			unsafe { std::slice::from_raw_parts(p.string_value as *const u8, p.string_length) };
		assert_eq!(unsafe { *p.string_value.add(p.string_length) }, 0);
		Some(bytes.to_vec())
	};
	Primitive {
		kind,
		int_value: p.int_value,
		float_value: p.float_value,
		string_value,
	}
}

fn entry_from_c(e: &ConfigEntry) -> Entry
{
	let name = unsafe { std::slice::from_raw_parts(e.name as *const u8, e.name_length) }.to_vec();
	let members = if e.member_count == 0 {
		Vec::new()
	} else {
		unsafe { std::slice::from_raw_parts(e.members, e.member_count) }
			.iter()
			.map(entry_from_c)
			.collect()
	};
	let array = if e.array_count == 0 {
		Vec::new()
	} else {
		unsafe { std::slice::from_raw_parts(e.array, e.array_count) }
			.iter()
			.map(primitive_from_c)
			.collect()
	};
	Entry {
		name,
		value: primitive_from_c(&e.value),
		is_array: e.is_array != 0,
		is_dot_reference: e.is_dot_reference != 0,
		members,
		array,
	}
}
