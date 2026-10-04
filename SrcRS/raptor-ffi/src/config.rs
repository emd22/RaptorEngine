use std::ffi::{CString, c_char, c_void};
use std::mem::{align_of, offset_of, size_of};

use crate::RxLogSink;
use std::panic::{AssertUnwindSafe, catch_unwind};

use raptor_config::host::{Host, LogLevel};
use raptor_config::model::{Entry, Kind, Primitive};
use raptor_config::writer;

pub type ReadIncludeFn = unsafe extern "C" fn(
	user: *mut c_void,
	path: *const c_char,
	extension: *const c_char,
	data: *mut *mut u8,
	length: *mut usize,
) -> i32;

pub type ReleaseIncludeFn = unsafe extern "C" fn(user: *mut c_void, data: *mut u8);

pub type LogFn = unsafe extern "C" fn(
	user: *mut c_void,
	level: i32,
	category: i32,
	message: *const c_char,
	length: usize,
);

#[repr(C)]
pub struct RxHost
{
	pub user: *mut c_void,
	pub read_include: Option<ReadIncludeFn>,
	pub release_include: Option<ReleaseIncludeFn>,
	pub log: Option<LogFn>,
}

#[repr(C)]
pub struct ConfigPrimitive
{
	pub int_value: i64,
	pub string_value: *const c_char,
	pub string_length: usize,
	pub float_value: f32,
	pub kind: u8,
}

#[repr(C)]
pub struct ConfigEntry
{
	pub name: *const c_char,
	pub name_length: usize,
	pub value: ConfigPrimitive,
	pub members: *const ConfigEntry,
	pub member_count: usize,
	pub array: *const ConfigPrimitive,
	pub array_count: usize,
	pub is_array: u8,
	pub is_dot_reference: u8,
}

const _: () = {
	assert!(size_of::<ConfigPrimitive>() == 32);
	assert!(align_of::<ConfigPrimitive>() == 8);
	assert!(offset_of!(ConfigPrimitive, string_value) == 8);
	assert!(offset_of!(ConfigPrimitive, string_length) == 16);
	assert!(offset_of!(ConfigPrimitive, float_value) == 24);
	assert!(offset_of!(ConfigPrimitive, kind) == 28);

	assert!(size_of::<ConfigEntry>() == 88);
	assert!(align_of::<ConfigEntry>() == 8);
	assert!(offset_of!(ConfigEntry, name_length) == 8);
	assert!(offset_of!(ConfigEntry, value) == 16);
	assert!(offset_of!(ConfigEntry, members) == 48);
	assert!(offset_of!(ConfigEntry, member_count) == 56);
	assert!(offset_of!(ConfigEntry, array) == 64);
	assert!(offset_of!(ConfigEntry, array_count) == 72);
	assert!(offset_of!(ConfigEntry, is_array) == 80);
	assert!(offset_of!(ConfigEntry, is_dot_reference) == 81);
};

struct CHost<'a>(&'a RxHost);

impl Host for CHost<'_>
{
	fn read_include(&mut self, path: &[u8], extension: &[u8]) -> Option<Vec<u8>>
	{
		let read = self.0.read_include?;
		let path = CString::new(path).ok()?;
		let extension = CString::new(extension).ok()?;

		let mut data: *mut u8 = std::ptr::null_mut();
		let mut length: usize = 0;

		// SAFETY: the host promised `read_include` accepts NUL-terminated strings and valid out
		// pointers.
		let found = unsafe {
			read(
				self.0.user,
				path.as_ptr(),
				extension.as_ptr(),
				&mut data,
				&mut length,
			)
		};
		if found == 0 {
			return None;
		}

		let bytes = if data.is_null() || length == 0 {
			Vec::new()
		} else {
			// SAFETY: on success the host returns `length` readable bytes at `data` until
			// `release_include`.
			unsafe { std::slice::from_raw_parts(data, length) }.to_vec()
		};

		if let Some(release) = self.0.release_include {
			// SAFETY: `data` came from this host's `read_include` and is released exactly once.
			unsafe { release(self.0.user, data) };
		}

		Some(bytes)
	}

	fn log(&mut self, level: LogLevel, category: i32, message: &[u8])
	{
		if let Some(log) = self.0.log {
			// SAFETY: the host promised `log` accepts a pointer and length pair.
			unsafe {
				log(
					self.0.user,
					level as i32,
					category,
					message.as_ptr().cast(),
					message.len(),
				)
			};
		}
	}
}

#[derive(Default)]
struct Pool
{
	bytes: Vec<Box<[u8]>>,
	entries: Vec<Box<[ConfigEntry]>>,
	primitives: Vec<Box<[ConfigPrimitive]>>,
}

impl Pool
{
	fn nul_terminated(&mut self, bytes: &[u8]) -> *const c_char
	{
		let mut owned = Vec::with_capacity(bytes.len() + 1);
		owned.extend_from_slice(bytes);
		owned.push(0);
		let boxed = owned.into_boxed_slice();
		let pointer = boxed.as_ptr().cast();
		self.bytes.push(boxed);
		pointer
	}

	fn primitive(&mut self, primitive: &Primitive) -> ConfigPrimitive
	{
		let (string_value, string_length) = match &primitive.string_value {
			Some(bytes) => (self.nul_terminated(bytes), bytes.len()),
			None => (std::ptr::null(), 0),
		};

		ConfigPrimitive {
			int_value: primitive.int_value,
			string_value,
			string_length,
			float_value: primitive.float_value,
			kind: primitive.kind as u8,
		}
	}

	fn entry(&mut self, entry: &Entry) -> ConfigEntry
	{
		let members = self.entries_slice(&entry.members);
		let array: Box<[ConfigPrimitive]> = entry.array.iter().map(|p| self.primitive(p)).collect();
		let array_count = array.len();
		let array_pointer = if array_count == 0 {
			std::ptr::null()
		} else {
			array.as_ptr()
		};
		self.primitives.push(array);

		ConfigEntry {
			name: self.nul_terminated(&entry.name),
			name_length: entry.name.len(),
			value: self.primitive(&entry.value),
			members: members.0,
			member_count: members.1,
			array: array_pointer,
			array_count,
			is_array: u8::from(entry.is_array),
			is_dot_reference: u8::from(entry.is_dot_reference),
		}
	}

	fn entries_slice(&mut self, entries: &[Entry]) -> (*const ConfigEntry, usize)
	{
		let converted: Box<[ConfigEntry]> = entries.iter().map(|e| self.entry(e)).collect();
		let count = converted.len();
		let pointer = if count == 0 {
			std::ptr::null()
		} else {
			converted.as_ptr()
		};
		self.entries.push(converted);
		(pointer, count)
	}
}

pub struct RxConfig
{
	root: (*const ConfigEntry, usize),
	has_errors: bool,
	_pool: Pool,
}

/// # Safety
///
/// `data` must point to `length` readable bytes. `prelude_path` may be null, otherwise it must be
/// NUL-terminated, as must `include_extension`. `host` must point to a valid `RxHost` whose
/// callbacks follow `raptor_ffi.h`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_config_parse(
	data: *const u8,
	length: usize,
	prelude_path: *const c_char,
	include_extension: *const c_char,
	host: *const RxHost,
) -> *mut RxConfig
{
	let result = catch_unwind(AssertUnwindSafe(|| {
		// SAFETY: guaranteed by the caller as documented above.
		let (data, host) = unsafe {
			let data = if data.is_null() {
				&[][..]
			} else {
				std::slice::from_raw_parts(data, length)
			};
			(data, &*host)
		};

		let prelude = if prelude_path.is_null() {
			None
		} else {
			// SAFETY: NUL-terminated by the caller.
			Some(unsafe { std::ffi::CStr::from_ptr(prelude_path) }.to_bytes())
		};
		let extension = if include_extension.is_null() {
			&[][..]
		} else {
			// SAFETY: NUL-terminated by the caller.
			unsafe { std::ffi::CStr::from_ptr(include_extension) }.to_bytes()
		};

		let mut c_host = CHost(host);
		let parsed = raptor_config::parse(data, prelude, extension, &mut c_host);

		let mut pool = Pool::default();
		let root = pool.entries_slice(&parsed.entries);

		Box::into_raw(Box::new(RxConfig {
			root,
			has_errors: parsed.has_errors,
			_pool: pool,
		}))
	}));

	result.unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `config` must come from `rx_config_parse` and not have been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_config_has_errors(config: *const RxConfig) -> i32
{
	// SAFETY: guaranteed by the caller.
	i32::from(unsafe { &*config }.has_errors)
}

/// # Safety
///
/// `config` must come from `rx_config_parse` and not have been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_config_entry_count(config: *const RxConfig) -> usize
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*config }.root.1
}

/// # Safety
///
/// `config` must come from `rx_config_parse` and not have been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_config_entries(config: *const RxConfig) -> *const ConfigEntry
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*config }.root.0
}

/// # Safety
///
/// `config` must be null or come from `rx_config_parse`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_config_free(config: *mut RxConfig)
{
	if !config.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(config) });
	}
}

const _: () = {
	assert!(Kind::None as u8 == 0);
	assert!(Kind::Int as u8 == 1);
	assert!(Kind::Float as u8 == 2);
	assert!(Kind::String as u8 == 3);
	assert!(Kind::Struct as u8 == 4);
};

fn primitive_from_c(primitive: &ConfigPrimitive) -> Primitive
{
	let kind = match primitive.kind {
		1 => Kind::Int,
		2 => Kind::Float,
		3 => Kind::String,
		4 => Kind::Struct,
		_ => Kind::None,
	};

	let string_value = if primitive.string_value.is_null() {
		None
	} else {
		// SAFETY: the caller promised `string_length` readable bytes at `string_value`.
		Some(
			unsafe {
				std::slice::from_raw_parts(
					primitive.string_value.cast::<u8>(),
					primitive.string_length,
				)
			}
			.to_vec(),
		)
	};

	Primitive {
		kind,
		int_value: primitive.int_value,
		float_value: primitive.float_value,
		string_value,
	}
}

/// # Safety
///
/// `entry` and everything it points to must be valid for the depth of the tree.
unsafe fn entry_from_c(entry: &ConfigEntry) -> Entry
{
	// SAFETY: guaranteed by the caller.
	let (members, array) = unsafe {
		let members: &[ConfigEntry] = if entry.members.is_null() {
			&[]
		} else {
			std::slice::from_raw_parts(entry.members, entry.member_count)
		};
		let array: &[ConfigPrimitive] = if entry.array.is_null() {
			&[]
		} else {
			std::slice::from_raw_parts(entry.array, entry.array_count)
		};
		(members, array)
	};

	Entry {
		// SAFETY: the caller promised `name_length` readable bytes at `name`.
		name: unsafe { std::slice::from_raw_parts(entry.name.cast::<u8>(), entry.name_length) }
			.to_vec(),
		value: primitive_from_c(&entry.value),
		is_array: entry.is_array != 0,
		is_dot_reference: entry.is_dot_reference != 0,
		// SAFETY: guaranteed by the caller.
		members: members.iter().map(|m| unsafe { entry_from_c(m) }).collect(),
		array: array.iter().map(primitive_from_c).collect(),
	}
}

struct LogOnlyHost<'a>(&'a RxLogSink);

impl Host for LogOnlyHost<'_>
{
	fn read_include(&mut self, _path: &[u8], _extension: &[u8]) -> Option<Vec<u8>>
	{
		None
	}

	fn log(&mut self, level: LogLevel, category: i32, message: &[u8])
	{
		if let Some(log) = self.0.log {
			// SAFETY: the host promised `log` accepts a pointer and length pair.
			unsafe {
				log(
					self.0.user,
					level as i32,
					category,
					message.as_ptr().cast(),
					message.len(),
				)
			};
		}
	}
}

pub struct RxText(Vec<u8>);

fn text(build: impl FnOnce() -> Vec<u8> + std::panic::UnwindSafe) -> *mut RxText
{
	catch_unwind(|| Box::into_raw(Box::new(RxText(build())))).unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `entries` must point to `count` valid entries (with all the memory they reference). `log` must
/// be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_config_format_file(
	entries: *const ConfigEntry,
	count: usize,
	log: *const RxLogSink,
) -> *mut RxText
{
	// SAFETY: guaranteed by the caller.
	let (entries, log) = unsafe {
		let entries = if entries.is_null() {
			&[][..]
		} else {
			std::slice::from_raw_parts(entries, count)
		};
		(entries, &*log)
	};

	let entries: Vec<Entry> = entries
		.iter()
		// SAFETY: guaranteed by the caller.
		.map(|e| unsafe { entry_from_c(e) })
		.collect();

	text(AssertUnwindSafe(move || {
		writer::format_file(&entries, &mut LogOnlyHost(log))
	}))
}

/// # Safety
///
/// `entry` must be valid (with all the memory it references). `log` must be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_config_format_entry(
	entry: *const ConfigEntry,
	indent: u32,
	log: *const RxLogSink,
) -> *mut RxText
{
	// SAFETY: guaranteed by the caller.
	let (entry, log) = unsafe { (entry_from_c(&*entry), &*log) };

	text(AssertUnwindSafe(move || {
		let mut out = Vec::new();
		writer::format_entry(&entry, indent, &mut out, &mut LogOnlyHost(log));
		out
	}))
}

/// # Safety
///
/// `primitive` must be valid. `log` must be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_config_format_primitive(
	primitive: *const ConfigPrimitive,
	log: *const RxLogSink,
) -> *mut RxText
{
	// SAFETY: guaranteed by the caller.
	let (primitive, log) = unsafe { (primitive_from_c(&*primitive), &*log) };

	text(AssertUnwindSafe(move || {
		let mut out = Vec::new();
		writer::format_primitive(&primitive, &mut out, &mut LogOnlyHost(log));
		out
	}))
}

/// # Safety
///
/// `text` must come from an `rx_config_format_*` function and not have been freed. `length` must be
/// writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_data(text: *const RxText, length: *mut usize) -> *const c_char
{
	// SAFETY: guaranteed by the caller.
	let (text, length) = unsafe { (&*text, &mut *length) };
	*length = text.0.len();
	text.0.as_ptr().cast()
}

/// # Safety
///
/// `text` must be null or come from an `rx_config_format_*` function, and must not be used
/// afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_free(text: *mut RxText)
{
	if !text.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(text) });
	}
}
