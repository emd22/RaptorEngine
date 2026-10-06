use std::cell::Cell;
use std::ffi::c_void;
use std::sync::{Arc, Mutex, PoisonError};

use raptor_script::natives;
use raptor_script::{ScriptId, ScriptManager};

use crate::state::{Event, ScriptInput, ScriptState};
use crate::system::{ScriptHost, WeaponScript};
use raptor_level::ScriptDef;

type InitFn = unsafe extern "C" fn(*mut c_void, *const ScriptDef, *mut ScriptState);
type UpdateFn =
	unsafe extern "C" fn(*mut c_void, *const ScriptDef, *mut ScriptState, *const ScriptInput, f32);

thread_local! {
	static ACTIVE_HOST: Cell<Option<*mut dyn ScriptHost>> = const { Cell::new(None) };
}

struct HostScope;

impl HostScope {
	fn enter(host: &mut dyn ScriptHost) -> Self {
		// SAFETY: the pointer is only read while the scope lives, which is within the borrow of
		// `host`, and it is cleared when the scope ends.
		let pointer: *mut (dyn ScriptHost + 'static) = unsafe {
			std::mem::transmute::<&mut dyn ScriptHost, &mut (dyn ScriptHost + 'static)>(host)
		};

		ACTIVE_HOST.with(|slot| slot.set(Some(pointer)));

		Self
	}
}

impl Drop for HostScope {
	fn drop(&mut self) {
		ACTIVE_HOST.with(|slot| slot.set(None));
	}
}

fn with_host<R>(fallback: R, f: impl FnOnce(&mut dyn ScriptHost) -> R) -> R {
	match ACTIVE_HOST.with(Cell::get) {
		// SAFETY: set by `HostScope` for the duration of a script call on this thread, so the
		// host is live and not otherwise borrowed while the script runs.
		Some(host) => f(unsafe { &mut *host }),
		None => fallback,
	}
}

extern "C" fn weapon_trace(yaw: f32, pitch: f32, range: f32) -> f32 {
	with_host(-1.0, |host| host.trace(yaw, pitch, range))
}

extern "C" fn weapon_hit_apply(damage: f32, force: f32, decal: bool) {
	with_host((), |host| host.apply_hit(damage, force, decal));
}

extern "C" fn weapon_kick(pitch: f32, yaw: f32) {
	with_host((), |host| host.kick(pitch, yaw));
}

extern "C" fn weapon_event(event: i32) {
	if let Some(event) = Event::from_raw(event) {
		with_host((), |host| host.event(event));
	}
}

pub fn register_natives() {
	natives::register("weapon_trace", weapon_trace as *const c_void);
	natives::register("weapon_hit_apply", weapon_hit_apply as *const c_void);
	natives::register("weapon_kick", weapon_kick as *const c_void);
	natives::register("weapon_event", weapon_event as *const c_void);
}

pub struct StrataWeapon {
	manager: Arc<Mutex<ScriptManager>>,
	id: ScriptId,
	init: Option<InitFn>,
	equip: Option<InitFn>,
	holster: Option<InitFn>,
	update: Option<UpdateFn>,
	name: String,
}

impl StrataWeapon {
	pub fn load(manager: Arc<Mutex<ScriptManager>>, path: &str, name: &str) -> Self {
		let id = manager
			.lock()
			.unwrap_or_else(PoisonError::into_inner)
			.load(path);

		let mut weapon = Self {
			manager,
			id,
			init: None,
			equip: None,
			holster: None,
			update: None,
			name: name.to_owned(),
		};

		weapon.bind();

		weapon
	}

	fn bind(&mut self) {
		let manager = self.manager.lock().unwrap_or_else(PoisonError::into_inner);

		let Some(script) = manager.get(self.id) else {
			return;
		};

		self.init = script.function::<InitFn>("weapon_init");
		self.equip = script.function::<InitFn>("weapon_equip");
		self.holster = script.function::<InitFn>("weapon_holster");
		self.update = script.function::<UpdateFn>("weapon_update");

		if self.update.is_none() {
			raptor_core::log_error!("Weapon '{}' has no weapon_update function", self.name);
		}
	}

	fn context(&self) -> Option<*mut c_void> {
		let manager = self.manager.lock().unwrap_or_else(PoisonError::into_inner);

		manager
			.get(self.id)
			.map(raptor_script::Script::global_context)
	}

	fn call(
		&self,
		function: Option<InitFn>,
		def: &ScriptDef,
		state: &mut ScriptState,
		host: &mut dyn ScriptHost,
	) -> bool {
		let (Some(function), Some(context)) = (function, self.context()) else {
			return false;
		};

		let _scope = HostScope::enter(host);

		// SAFETY: the function was looked up for this signature, and the pointers are live.
		unsafe { function(context, def, state) };

		true
	}
}

impl WeaponScript for StrataWeapon {
	fn init(&mut self, def: &ScriptDef, state: &mut ScriptState, host: &mut dyn ScriptHost) {
		self.call(self.init, def, state, host);
	}

	fn equip(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		host: &mut dyn ScriptHost,
	) -> bool {
		self.call(self.equip, def, state, host)
	}

	fn holster(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		host: &mut dyn ScriptHost,
	) -> bool {
		self.call(self.holster, def, state, host)
	}

	fn update(
		&mut self,
		def: &ScriptDef,
		state: &mut ScriptState,
		input: &ScriptInput,
		delta_time: f32,
		host: &mut dyn ScriptHost,
	) -> bool {
		let (Some(function), Some(context)) = (self.update, self.context()) else {
			return false;
		};

		let _scope = HostScope::enter(host);

		// SAFETY: the function was looked up for this signature, and the pointers are live.
		unsafe { function(context, def, state, input, delta_time) };

		true
	}

	fn rebind(&mut self) {
		self.bind();
	}
}
