pub mod natives;
pub mod sys;

use std::ffi::{CStr, CString, c_char, c_void};
use std::path::{Path, PathBuf};
use std::ptr;

pub struct Compiler {
	raw: *mut sys::StrataCompiler,
}

impl Compiler {
	pub fn new() -> Option<Self> {
		// SAFETY: creating a compiler has no preconditions.
		let raw = unsafe { sys::strataCompilerCreate() };

		(!raw.is_null()).then_some(Self { raw })
	}
}

impl Drop for Compiler {
	fn drop(&mut self) {
		// SAFETY: `raw` came from `strataCompilerCreate` and is destroyed exactly once.
		unsafe { sys::strataCompilerDestroy(self.raw) };
	}
}

pub struct Script {
	path: PathBuf,
	jit: *mut sys::StrataJit,
	errors: Option<String>,
	context: *mut c_void,
}

impl Script {
	fn load(compiler: &Compiler, path: &Path) -> Self {
		let mut script = Self {
			path: path.to_owned(),
			jit: ptr::null_mut(),
			errors: None,
			context: ptr::null_mut(),
		};

		script.compile(compiler);

		script
	}

	fn release(&mut self) {
		if !self.context.is_null() {
			if let Some(destroy) =
				self.function::<unsafe extern "C" fn(*mut c_void)>("__strata_context_destroy")
			{
				// SAFETY: the context came from this module's `__strata_context_create` and the JIT is alive.
				unsafe { destroy(self.context) };
			}

			self.context = ptr::null_mut();
		}

		if !self.jit.is_null() {
			// SAFETY: `jit` came from a Strata compile call and is destroyed exactly once.
			unsafe { sys::strataJitDestroy(self.jit) };
			self.jit = ptr::null_mut();
		}

		self.errors = None;
	}

	fn compile(&mut self, compiler: &Compiler) {
		self.release();

		let Ok(path) = CString::new(self.path.to_string_lossy().into_owned()) else {
			self.errors = Some("script path contains a NUL byte".to_owned());
			return;
		};

		let mut errors: *const c_char = ptr::null();

		// SAFETY: `compiler` is live and `path` is NUL terminated.
		self.jit = unsafe { sys::strataJitCompileFile(compiler.raw, path.as_ptr(), &mut errors) };

		if !errors.is_null() {
			// SAFETY: Strata returns a NUL terminated message that we free right after copying it.
			let message = unsafe { CStr::from_ptr(errors) }
				.to_string_lossy()
				.into_owned();

			// SAFETY: `errors` was allocated by Strata and is freed exactly once.
			unsafe { sys::strataFree(errors.cast_mut()) };

			self.errors = Some(message);
		}

		if self.errors.is_some() || self.jit.is_null() {
			if self.errors.is_none() {
				self.errors = Some("script failed to compile".to_owned());
			}

			return;
		}

		if let Some(create) =
			self.function::<unsafe extern "C" fn() -> *mut c_void>("__strata_context_create")
		{
			// SAFETY: the symbol is the module's context constructor.
			self.context = unsafe { create() };
		}

		self.bind_externs();
	}

	fn bind_externs(&mut self) {
		// SAFETY: `jit` is a live module.
		let count = unsafe { sys::strataJitGetExternSymbolCount(self.jit) };

		for index in 0..count {
			// SAFETY: `index` is below the extern count.
			let name = unsafe { sys::strataJitGetExternSymbolName(self.jit, index) };

			if name.is_null() {
				continue;
			}

			// SAFETY: Strata returns a NUL terminated name that lives as long as the module.
			let name = unsafe { CStr::from_ptr(name) };

			let bound = natives::find(name).is_some_and(|function| {
				// SAFETY: `jit` is live and `name` is NUL terminated.
				unsafe {
					sys::strataJitAddSymbol(self.jit, name.as_ptr(), function.cast_mut()) != 0
				}
			});

			if !bound {
				eprintln!(
					"[script] No host binding for extern '{}'",
					name.to_string_lossy()
				);
			}
		}
	}

	pub fn path(&self) -> &Path {
		&self.path
	}

	pub fn errors(&self) -> Option<&str> {
		self.errors.as_deref()
	}

	pub fn has_errors(&self) -> bool {
		self.errors.is_some()
	}

	pub fn global_context(&self) -> *mut c_void {
		self.context
	}

	pub fn function_ptr(&self, name: &str) -> Option<*const c_void> {
		if self.jit.is_null() || self.errors.is_some() {
			return None;
		}

		let name = CString::new(name).ok()?;

		// SAFETY: `jit` is live and `name` is NUL terminated.
		let function = unsafe { sys::strataJitGetFunction(self.jit, name.as_ptr()) };

		(!function.is_null()).then_some(function.cast_const())
	}

	pub fn function<F: Copy>(&self, name: &str) -> Option<F> {
		assert_eq!(size_of::<F>(), size_of::<*const c_void>());

		let function = self.function_ptr(name)?;

		// SAFETY: `F` is a function pointer type of pointer size, chosen by the caller to match the script
		// function's signature.
		Some(unsafe { std::mem::transmute_copy::<*const c_void, F>(&function) })
	}

	pub fn call(&self, name: &str) -> bool {
		match self.function::<unsafe extern "C" fn(*mut c_void)>(name) {
			Some(function) => {
				// SAFETY: a script function with no parameters takes only the context.
				unsafe { function(self.context) };
				true
			}
			None => false,
		}
	}
}

impl Drop for Script {
	fn drop(&mut self) {
		self.release();
	}
}

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct ScriptId(usize);

impl ScriptId {
	pub fn from_index(index: usize) -> Self {
		Self(index)
	}

	pub fn index(self) -> usize {
		self.0
	}
}

pub struct ScriptManager {
	compiler: Option<Compiler>,
	scripts: Vec<Option<Script>>,
}

impl Default for ScriptManager {
	fn default() -> Self {
		Self::new()
	}
}

impl ScriptManager {
	pub fn new() -> Self {
		Self {
			compiler: Compiler::new(),
			scripts: Vec::new(),
		}
	}

	pub fn load(&mut self, path: impl AsRef<Path>) -> ScriptId {
		let script = match &self.compiler {
			Some(compiler) => Script::load(compiler, path.as_ref()),
			None => Script {
				path: path.as_ref().to_owned(),
				jit: ptr::null_mut(),
				errors: Some("no Strata compiler".to_owned()),
				context: ptr::null_mut(),
			},
		};

		if let Some(errors) = script.errors() {
			eprintln!(
				"[script] Could not compile script '{}'\nErrors:\n{errors}",
				script.path().display()
			);
		}

		match self.scripts.iter().position(Option::is_none) {
			Some(slot) => {
				self.scripts[slot] = Some(script);
				ScriptId(slot)
			}
			None => {
				self.scripts.push(Some(script));
				ScriptId(self.scripts.len() - 1)
			}
		}
	}

	pub fn get(&self, id: ScriptId) -> Option<&Script> {
		self.scripts.get(id.0).and_then(Option::as_ref)
	}

	pub fn free(&mut self, id: ScriptId) {
		if let Some(slot) = self.scripts.get_mut(id.0) {
			*slot = None;
		}
	}

	pub fn reload(&mut self, id: ScriptId) {
		let Some(compiler) = &self.compiler else {
			return;
		};

		if let Some(Some(script)) = self.scripts.get_mut(id.0) {
			script.compile(compiler);

			if let Some(errors) = script.errors() {
				eprintln!(
					"[script] Could not compile script '{}'\nErrors:\n{errors}",
					script.path().display()
				);
			}
		}
	}

	pub fn reload_all(&mut self) {
		for index in 0..self.scripts.len() {
			self.reload(ScriptId(index));
		}
	}
}

impl Drop for ScriptManager {
	fn drop(&mut self) {
		self.scripts.clear();
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	fn script_file(name: &str, source: &str) -> PathBuf {
		let path =
			std::env::temp_dir().join(format!("raptor_script_{}_{name}.st", std::process::id()));

		std::fs::write(&path, source).unwrap();

		path
	}

	#[test]
	fn a_script_compiles_and_runs() {
		let path = script_file("add", "int add(int a, int b) { return a + b; }\n");
		let mut manager = ScriptManager::new();
		let id = manager.load(&path);
		let script = manager.get(id).unwrap();

		assert!(!script.has_errors(), "{:?}", script.errors());

		let add = script
			.function::<unsafe extern "C" fn(*mut c_void, i32, i32) -> i32>("add")
			.unwrap();

		// SAFETY: `add` has the signature the script declared.
		assert_eq!(unsafe { add(script.global_context(), 2, 3) }, 5);

		assert!(script.function_ptr("missing").is_none());

		let _ = std::fs::remove_file(path);
	}

	#[test]
	fn a_broken_script_reports_errors_and_reload_recovers() {
		let path = script_file("broken", "int nope( {\n");
		let mut manager = ScriptManager::new();
		let id = manager.load(&path);

		assert!(manager.get(id).unwrap().has_errors());
		assert!(manager.get(id).unwrap().function_ptr("nope").is_none());

		std::fs::write(&path, "int one() { return 1; }\n").unwrap();
		manager.reload_all();

		let script = manager.get(id).unwrap();

		assert!(!script.has_errors(), "{:?}", script.errors());
		assert!(script.function_ptr("one").is_some());

		let _ = std::fs::remove_file(path);
	}
}
