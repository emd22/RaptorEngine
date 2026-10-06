use std::ffi::{CStr, c_char, c_void};
use std::path::PathBuf;
use std::sync::{Mutex, Once};

use raptor_script::{ScriptId, ScriptManager};

struct Manager(ScriptManager);

// SAFETY: the manager is only touched while holding the mutex, and Strata modules have no thread affinity.
unsafe impl Send for Manager {}

static MANAGER: Mutex<Option<Manager>> = Mutex::new(None);

fn init_natives()
{
	static INIT: Once = Once::new();

	INIT.call_once(|| {
		raptor_script::natives::register("random_range", crate::random::rx_random_range as *const c_void);
	});
}

fn with_manager<R>(action: impl FnOnce(&mut ScriptManager) -> R) -> Option<R>
{
	init_natives();

	let mut guard = MANAGER.lock().ok()?;

	let manager = guard.get_or_insert_with(|| Manager(ScriptManager::new()));

	Some(action(&mut manager.0))
}

/// # Safety
///
/// `name` must be NUL-terminated and `function` a function with the C ABI the script expects.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_script_register_extern(name: *const c_char, function: *const c_void)
{
	init_natives();

	// SAFETY: guaranteed by the caller.
	let name = unsafe { CStr::from_ptr(name) };

	raptor_script::natives::register(&name.to_string_lossy(), function);
}

/// # Safety
///
/// `name` must be NUL-terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_script_find_extern(name: *const c_char) -> *const c_void
{
	init_natives();

	// SAFETY: guaranteed by the caller.
	let name = unsafe { CStr::from_ptr(name) };

	raptor_script::natives::find(name).unwrap_or(std::ptr::null())
}

/// # Safety
///
/// `path` must be NUL-terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_script_load(path: *const c_char) -> u32
{
	// SAFETY: guaranteed by the caller.
	let path = PathBuf::from(unsafe { CStr::from_ptr(path) }.to_string_lossy().into_owned());

	with_manager(|manager| manager.load(path).index() as u32).unwrap_or(u32::MAX)
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_script_free(id: u32)
{
	with_manager(|manager| manager.free(ScriptId::from_index(id as usize)));
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_script_reload(id: u32)
{
	with_manager(|manager| manager.reload(ScriptId::from_index(id as usize)));
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_script_reload_all()
{
	with_manager(ScriptManager::reload_all);
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_script_has_errors(id: u32) -> bool
{
	with_manager(|manager| {
		manager
			.get(ScriptId::from_index(id as usize))
			.is_some_and(raptor_script::Script::has_errors)
	})
	.unwrap_or(false)
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_script_context(id: u32) -> *mut c_void
{
	with_manager(|manager| {
		manager
			.get(ScriptId::from_index(id as usize))
			.map_or(std::ptr::null_mut(), raptor_script::Script::global_context)
	})
	.unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `name` must be NUL-terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_script_function(id: u32, name: *const c_char) -> *const c_void
{
	// SAFETY: guaranteed by the caller.
	let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned();

	with_manager(|manager| {
		manager
			.get(ScriptId::from_index(id as usize))
			.and_then(|script| script.function_ptr(&name))
	})
	.flatten()
	.unwrap_or(std::ptr::null())
}

pub fn shutdown_scripts()
{
	if let Ok(mut guard) = MANAGER.lock() {
		*guard = None;
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_script_shutdown()
{
	shutdown_scripts();
}
