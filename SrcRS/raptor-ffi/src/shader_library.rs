use std::ffi::{CStr, c_char};
use std::sync::Arc;

use raptor_gpu::ShaderProgramRecord;
use raptor_render::shader_library::{LoadError, ShaderLibrary};
use raptor_shader::preproc::Stage;
use raptor_shader::{Log, LogLevel};

use crate::gpu::RxGpuDevice;
use crate::gpu_pipelines::{RxShaderMacroRef, macro_refs};
use crate::{LogFn, RxLogSink};

pub type RxShaderLibrary = ShaderLibrary;

const CATEGORY_SHADER: i32 = 1;

pub const LOAD_OK: i32 = 0;
pub const LOAD_COMPILE_FAILED: i32 = 1;
pub const LOAD_COMPILER_UNAVAILABLE: i32 = 2;

pub(crate) struct SinkLog<'a>(pub(crate) &'a RxLogSink);

impl Log for SinkLog<'_>
{
	fn log(&mut self, level: LogLevel, message: &str)
	{
		let Some(log): Option<LogFn> = self.0.log else {
			return;
		};

		let level = match level {
			LogLevel::Print => 0,
			LogLevel::Warning => 2,
			LogLevel::Error => 3,
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
/// `directory` must be NUL terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shader_library_new(directory: *const c_char) -> *mut RxShaderLibrary
{
	// SAFETY: guaranteed by the caller.
	let directory = unsafe { CStr::from_ptr(directory) }.to_string_lossy();

	Box::into_raw(Box::new(ShaderLibrary::new(&directory)))
}

/// Destroys the library and the programs it made that nothing else holds. `device` may be null to
/// leak them.
///
/// # Safety
///
/// `library` must be null or come from `rx_shader_library_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shader_library_free(
	library: *mut RxShaderLibrary,
	device: *const RxGpuDevice,
)
{
	if library.is_null() {
		return;
	}

	// SAFETY: guaranteed by the caller.
	let library = unsafe { *Box::from_raw(library) };

	// SAFETY: guaranteed by the caller.
	if let Some(device) = unsafe { device.as_ref() } {
		// SAFETY: guaranteed by the caller.
		unsafe { library.destroy(&device.device) };
	}
}

/// Gets the program of one stage (a shader type bit) of the shader `name` for `macros`, compiling
/// the shader the first time a set of macros is asked for. The program has a reference for the
/// caller, to be given back with `rx_shader_program_release`. It is null if the shader has no such
/// stage, or if `out_status` is not 0.
///
/// # Safety
///
/// `library` and `device` must be live, `name` NUL terminated, `macros` valid for `macro_count`
/// entries whose strings are null or NUL terminated, `log` a valid sink and `out_status` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shader_library_get_program(
	library: *mut RxShaderLibrary,
	device: *const RxGpuDevice,
	name: *const c_char,
	stage_bits: u32,
	macros: *const RxShaderMacroRef,
	macro_count: usize,
	log: *const RxLogSink,
	out_status: *mut i32,
) -> *mut ShaderProgramRecord
{
	let stage = match stage_bits {
		1 => Stage::Vertex,
		2 => Stage::Pixel,
		4 => Stage::Compute,
		_ => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out_status = LOAD_OK };
			return std::ptr::null_mut();
		}
	};

	// SAFETY: guaranteed by the caller.
	let (name, macros, mut log) = unsafe {
		(
			CStr::from_ptr(name).to_string_lossy(),
			macro_refs(macros, macro_count),
			SinkLog(&*log),
		)
	};

	// SAFETY: guaranteed by the caller.
	let result = unsafe { &mut *library }.program(
		unsafe { &(*device).device },
		&name,
		stage,
		&macros,
		&mut log,
	);

	let (status, program) = match result {
		Ok(program) => (LOAD_OK, program),
		Err(LoadError::CompileFailed) => (LOAD_COMPILE_FAILED, None),
		Err(LoadError::CompilerUnavailable) => (LOAD_COMPILER_UNAVAILABLE, None),
	};

	// SAFETY: guaranteed by the caller.
	unsafe { *out_status = status };

	program.map_or(std::ptr::null_mut(), |program| {
		ShaderProgramRecord::into_raw(Arc::clone(&program))
	})
}
