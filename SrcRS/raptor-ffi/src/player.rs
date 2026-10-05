use std::ffi::{CStr, CString, c_char};
use std::mem::size_of;
use std::panic::{AssertUnwindSafe, catch_unwind};

use raptor_level::{ScriptDef, WeaponDef, WeaponError};
use raptor_player::{Recoil, ViewKick, ViewSway};

use crate::RxHost;
use crate::config::CHost;

pub const RX_WEAPON_ERROR_PARSE: i32 = 1;
pub const RX_WEAPON_ERROR_NO_SCRIPT: i32 = 2;

#[repr(C)]
pub struct RxWeaponDef
{
	pub name: *const c_char,
	pub script: *const c_char,
	pub idle_anim: *const c_char,
	pub fire_anim: *const c_char,
	pub reload_anim: *const c_char,
	pub slot: i32,
	pub view_kick: f32,
	pub def: ScriptDef,
}

pub struct RxWeaponDefOwner
{
	def: RxWeaponDef,
	_text: Vec<CString>,
}

const _: () = {
	assert!(size_of::<ScriptDef>() == 140);
	assert!(size_of::<RxWeaponDef>() == 5 * 8 + 8 + 140 + 4);
	assert!(size_of::<ViewKick>() == 48);
	assert!(size_of::<Recoil>() == 24);
	assert!(size_of::<ViewSway>() == 16);
};

fn c_text(text: &str, store: &mut Vec<CString>) -> *const c_char
{
	let text = CString::new(text.replace('\0', "")).unwrap_or_default();
	let pointer = text.as_ptr();

	store.push(text);

	pointer
}

/// Parses the text of a weapon config. On failure it returns null and sets `error` to one of
/// `RX_WEAPON_ERROR_*`.
///
/// # Safety
///
/// `data` must point at `length` readable bytes, `prelude_path` must be null or NUL-terminated,
/// `host` must be valid and `error` must be null or writable. The result lasts until
/// `rx_weapon_def_free`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_weapon_def_parse(
	data: *const u8,
	length: usize,
	prelude_path: *const c_char,
	host: *const RxHost,
	error: *mut i32,
) -> *mut RxWeaponDefOwner
{
	catch_unwind(AssertUnwindSafe(|| {
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

		match WeaponDef::parse(data, prelude, &mut host) {
			Ok(weapon) => {
				let mut text = Vec::new();

				let def = RxWeaponDef {
					name: c_text(&weapon.name, &mut text),
					script: c_text(&weapon.script, &mut text),
					idle_anim: c_text(&weapon.idle_anim, &mut text),
					fire_anim: c_text(&weapon.fire_anim, &mut text),
					reload_anim: c_text(&weapon.reload_anim, &mut text),
					slot: weapon.slot,
					view_kick: weapon.view_kick,
					def: weapon.def,
				};

				Box::into_raw(Box::new(RxWeaponDefOwner { def, _text: text }))
			}
			Err(failure) => {
				if !error.is_null() {
					let code = match failure {
						WeaponError::Parse => RX_WEAPON_ERROR_PARSE,
						WeaponError::NoScript => RX_WEAPON_ERROR_NO_SCRIPT,
					};

					// SAFETY: guaranteed by the caller.
					unsafe { *error = code };
				}

				std::ptr::null_mut()
			}
		}
	}))
	.unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `weapon` must be live. The definition lasts until `rx_weapon_def_free`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_weapon_def_get(weapon: *const RxWeaponDefOwner) -> *const RxWeaponDef
{
	// SAFETY: guaranteed by the caller.
	&raw const unsafe { &*weapon }.def
}

/// # Safety
///
/// `weapon` must be null or come from `rx_weapon_def_parse`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_weapon_def_free(weapon: *mut RxWeaponDefOwner)
{
	if !weapon.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(weapon) });
	}
}

/// # Safety
///
/// `kick` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_view_kick_fire(
	kick: *mut ViewKick,
	kick_degrees: f32,
	kickback: f32,
	random_yaw: f32,
	random_roll: f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *kick }.fire(kick_degrees, kickback, random_yaw, random_roll);
}

/// # Safety
///
/// `kick` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_view_kick_update(kick: *mut ViewKick, delta_time: f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *kick }.update(delta_time);
}

/// # Safety
///
/// `recoil` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_recoil_add(recoil: *mut Recoil, pitch: f32, yaw: f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *recoil }.add(pitch, yaw);
}

/// # Safety
///
/// `recoil` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_recoil_cancel(recoil: *mut Recoil, yaw: f32, pitch: f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *recoil }.cancel(yaw, pitch);
}

/// Advances the recoil and writes how far the camera should turn.
///
/// # Safety
///
/// `recoil` must be live and the outputs writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_recoil_update(
	recoil: *mut Recoil,
	delta_time: f32,
	delta_yaw: *mut f32,
	delta_pitch: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (yaw, pitch) = unsafe { &mut *recoil }.update(delta_time);

	// SAFETY: guaranteed by the caller.
	unsafe {
		*delta_yaw = yaw;
		*delta_pitch = pitch;
	}
}

/// # Safety
///
/// `recoil` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_recoil_pitch_clamped(
	recoil: *mut Recoil,
	delta_pitch: f32,
	applied_pitch: f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *recoil }.pitch_clamped(delta_pitch, applied_pitch);
}

/// # Safety
///
/// `sway` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_view_sway_update(
	sway: *mut ViewSway,
	camera_yaw: f32,
	camera_pitch: f32,
	delta_time: f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *sway }.update(camera_yaw, camera_pitch, delta_time);
}
