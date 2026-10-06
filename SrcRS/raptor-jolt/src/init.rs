use std::sync::OnceLock;

use oxijolt_sys::*;

pub fn ensure_initialized() -> bool {
	static INIT: OnceLock<bool> = OnceLock::new();

	*INIT.get_or_init(|| {
		// SAFETY: called once, before any other Jolt call, through the `OnceLock`.
		unsafe { JPH_Init() }
	})
}

pub fn lock_globals() -> std::sync::MutexGuard<'static, ()> {
	static GLOBALS: std::sync::Mutex<()> = std::sync::Mutex::new(());

	GLOBALS
		.lock()
		.unwrap_or_else(std::sync::PoisonError::into_inner)
}
