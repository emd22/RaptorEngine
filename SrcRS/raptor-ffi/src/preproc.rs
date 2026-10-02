use std::ffi::{CStr, c_char, c_void};
use std::mem::{align_of, size_of};
use std::panic::{AssertUnwindSafe, catch_unwind};

use raptor_shader::{Log, LogLevel, Macro, Output, process};

use crate::LogFn;

const CATEGORY_SHADER: i32 = 1;
const LEVEL_PRINT: i32 = 0;
const LEVEL_ERROR: i32 = 3;

#[repr(C)]
pub struct RxLogSink {
	pub user: *mut c_void,
	pub log: Option<LogFn>,
}

#[repr(C)]
pub struct RxShaderMacro {
	pub name: *const c_char,
	pub value: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxReflectionEntry {
	pub kind: u16,
	pub set: u8,
	pub binding: u8,
}

const _: () = {
	assert!(size_of::<RxReflectionEntry>() == 4);
	assert!(align_of::<RxReflectionEntry>() == 2);
};

pub struct RxPreprocResult {
	output: Output,
	reflection: [Vec<RxReflectionEntry>; 3],
}

struct CLog<'a>(&'a RxLogSink);

impl Log for CLog<'_> {
	/// Logs a message to the shader channel in the engine.
	fn log(&mut self, level: LogLevel, message: &str) {
		let Some(log) = self.0.log else { return };

		let level = match level {
			LogLevel::Print => LEVEL_PRINT,
			LogLevel::Error => LEVEL_ERROR,
		};

		// SAFETY: the host promised `log` accepts a pointer and length pair.
		unsafe {
			log(
				self.0.user,
				level,
				CATEGORY_SHADER,
				message.as_ptr().cast(),
				message.len(),
			)
		};
	}
}

/// # Safety
///
/// `data` must point to `length` readable bytes. `macros` must point to `macro_count` entries whose names are
/// NUL-terminated and whose values are NUL-terminated or null. `log` must point to a valid `RxLogSink`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_preproc_process(
	data: *const u8,
	length: usize,
	macros: *const RxShaderMacro,
	macro_count: usize,
	log: *const RxLogSink,
) -> *mut RxPreprocResult {
	let result = catch_unwind(AssertUnwindSafe(|| {
		// SAFETY: guaranteed by the caller as documented above.
		let (source, raw_macros, log) = unsafe {
			let source = if data.is_null() {
				&[][..]
			} else {
				std::slice::from_raw_parts(data, length)
			};
			let raw_macros = if macros.is_null() {
				&[][..]
			} else {
				std::slice::from_raw_parts(macros, macro_count)
			};
			(source, raw_macros, &*log)
		};

		let macros: Vec<Macro> = raw_macros
			.iter()
			.map(|m| Macro {
				// SAFETY: names and values are null terminated, values may be null.
				name: unsafe { CStr::from_ptr(m.name) }.to_bytes(),
				value: if m.value.is_null() {
					None
				} else {
					// SAFETY: see message above.
					Some(unsafe { CStr::from_ptr(m.value) }.to_bytes())
				},
			})
			.collect();

		let output = process(source, &macros, &mut CLog(log));

		let reflection = std::array::from_fn(|stage| {
			output.reflection[stage]
				.iter()
				.map(|e| RxReflectionEntry {
					kind: e.kind as u16,
					set: e.set,
					binding: e.binding,
				})
				.collect()
		});

		Box::into_raw(Box::new(RxPreprocResult { output, reflection }))
	}));

	result.unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `result` must come from `rx_preproc_process` and not have been freed. `length` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_preproc_program(
	result: *const RxPreprocResult,
	stage: u32,
	length: *mut usize,
) -> *const u8 {
	// SAFETY: guaranteed by the caller.
	let (result, length) = unsafe { (&*result, &mut *length) };

	match result.output.programs.get(stage as usize) {
		Some(program) => {
			*length = program.len();
			program.as_ptr()
		}
		None => {
			*length = 0;
			std::ptr::null()
		}
	}
}

/// # Safety
///
/// `result` must come from `rx_preproc_process` and not have been freed. `count` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_preproc_reflection(
	result: *const RxPreprocResult,
	stage: u32,
	count: *mut usize,
) -> *const RxReflectionEntry {
	// SAFETY: guaranteed by the caller.
	let (result, count) = unsafe { (&*result, &mut *count) };

	match result.reflection.get(stage as usize) {
		Some(entries) => {
			*count = entries.len();
			entries.as_ptr()
		}
		None => {
			*count = 0;
			std::ptr::null()
		}
	}
}

/// # Safety
///
/// `result` must be null or come from `rx_preproc_process`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_preproc_free(result: *mut RxPreprocResult) {
	if !result.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(result) });
	}
}
