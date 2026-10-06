use std::ffi::{c_char, c_void};

#[repr(C)]
pub struct StrataCompiler {
	_private: [u8; 0],
}

#[repr(C)]
pub struct StrataJit {
	_private: [u8; 0],
}

unsafe extern "C" {
	pub fn strataCompilerCreate() -> *mut StrataCompiler;
	pub fn strataCompilerDestroy(compiler: *mut StrataCompiler);
	pub fn strataJitCompileString(
		compiler: *mut StrataCompiler,
		source: *const c_char,
		module_name: *const c_char,
		err_out: *mut *const c_char,
	) -> *mut StrataJit;
	pub fn strataJitCompileFile(
		compiler: *mut StrataCompiler,
		path: *const c_char,
		err_out: *mut *const c_char,
	) -> *mut StrataJit;
	pub fn strataJitGetFunction(jit: *mut StrataJit, name: *const c_char) -> *mut c_void;
	pub fn strataJitAddSymbol(
		jit: *mut StrataJit,
		name: *const c_char,
		function: *mut c_void,
	) -> i32;
	pub fn strataJitGetExternSymbolCount(jit: *mut StrataJit) -> usize;
	pub fn strataJitGetExternSymbolName(jit: *mut StrataJit, index: usize) -> *const c_char;
	pub fn strataJitDestroy(jit: *mut StrataJit);
	pub fn strataFree(text: *mut c_char);
}
