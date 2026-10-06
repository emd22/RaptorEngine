use std::ffi::{CStr, CString, c_char};
use std::mem::{offset_of, size_of};
use std::panic::{AssertUnwindSafe, catch_unwind};

use raptor_entity::CameraCore;
use raptor_level::{ScriptDef, WeaponDef, WeaponError};
use raptor_math::Vec3f;
use raptor_player::locomotion::fov_step;
use raptor_player::{Locomotion, PlayerState, Recoil, ViewKick, ViewSway};

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
	assert!(size_of::<Locomotion>() == 20);
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
		} else {
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

/// Eases the velocity towards the input and writes it to `force`.
///
/// # Safety
///
/// `loco` must be live, `direction` and `offset` must point at three floats and `force` at three
/// writable floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_locomotion_step(
	loco: *mut Locomotion,
	delta_time: f64,
	direction: *const f32,
	offset: *const f32,
	sprinting: u8,
	speed_multiplier: f32,
	force: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (loco, direction, offset) = unsafe {
		(
			&mut *loco,
			*direction.cast::<[f32; 3]>(),
			*offset.cast::<[f32; 3]>(),
		)
	};

	let result = loco.step(
		delta_time,
		direction,
		offset,
		sprinting != 0,
		speed_multiplier,
	);

	// SAFETY: guaranteed by the caller.
	unsafe { *force.cast::<[f32; 3]>() = result };
}

/// # Safety
///
/// `loco` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_locomotion_released(loco: *const Locomotion) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*loco }.is_released())
}

/// # Safety
///
/// `loco` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_locomotion_bob(loco: *mut Locomotion, delta_time: f64)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *loco }.bob(delta_time);
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_fov_step(fov: f32, sprinting: u8, moving: u8, delta_time: f64) -> f32
{
	fov_step(fov, sprinting != 0, moving != 0, delta_time)
}

/// # Safety
///
/// `loco` must be live and `x` and `y` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_locomotion_head_bob(
	loco: *const Locomotion,
	strength_x: f32,
	strength_y: f32,
	x: *mut f32,
	y: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (bob_x, bob_y) = unsafe { &*loco }.head_bob(strength_x, strength_y);

	// SAFETY: guaranteed by the caller.
	unsafe {
		*x = bob_x;
		*y = bob_y;
	}
}

pub type RxPlayerState = PlayerState;

const _: () = {
	assert!(size_of::<PlayerState>() == 208);
	assert!(offset_of!(PlayerState, speed_multiplier) == 64);
	assert!(offset_of!(PlayerState, sprinting) == 72);
	assert!(offset_of!(PlayerState, locomotion) == 88);
};

fn vec3(values: *const f32) -> Vec3f
{
	// SAFETY: callers pass three floats.
	Vec3f::from_array(unsafe { *values.cast::<[f32; 3]>() })
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_player_state_new() -> *mut RxPlayerState
{
	Box::into_raw(Box::default())
}

/// # Safety
///
/// `state` must be null or come from `rx_player_state_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_state_free(state: *mut RxPlayerState)
{
	if !state.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(state) });
	}
}

/// # Safety
///
/// `state` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_set_fly_mode(state: *mut RxPlayerState, value: u8)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.set_fly_mode(value != 0);
}

/// # Safety
///
/// `state` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_is_fly_mode(state: *const RxPlayerState) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*state }.is_fly_mode())
}

/// # Safety
///
/// `state` and `camera` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_start_sway(state: *mut RxPlayerState, camera: *const CameraCore)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.start_sway_at(unsafe { &*camera });
}

/// # Safety
///
/// `state` and `camera` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_rotate_camera(
	state: *mut RxPlayerState,
	camera: *mut CameraCore,
	yaw: f32,
	pitch: f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.rotate_camera(unsafe { &mut *camera }, yaw, pitch);
}

/// # Safety
///
/// `state` and `camera` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_rotate_head(
	state: *mut RxPlayerState,
	camera: *mut CameraCore,
	yaw: f32,
	pitch: f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.rotate_head(unsafe { &mut *camera }, yaw, pitch);
}

/// # Safety
///
/// `state` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_jump(state: *mut RxPlayerState, grounded: u8)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.jump(grounded != 0);
}

/// # Safety
///
/// `state` must be live and `by` hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_move_by(state: *mut RxPlayerState, by: *const f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.move_by(vec3(by));
}

/// # Safety
///
/// `state` must be live, `input` hold three floats and `out` be writable for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_movement_force(
	state: *mut RxPlayerState,
	delta_time: f64,
	input: *const f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let force = unsafe { &mut *state }.movement_force(delta_time, vec3(input).to_array());

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts_mut(out, 3) }.copy_from_slice(&force);
}

/// # Safety
///
/// `state` and `camera` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_update(
	state: *mut RxPlayerState,
	camera: *mut CameraCore,
	delta_time: f64,
	grounded: u8,
	head_bob_enabled: u8,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.update(
		unsafe { &mut *camera },
		delta_time,
		grounded != 0,
		head_bob_enabled != 0,
	);
}

/// # Safety
///
/// `state` and `camera` must be live, `velocity` hold three floats, `out_position` be writable
/// for three and `out_rotation` for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_player_view_model_pose(
	state: *mut RxPlayerState,
	camera: *const CameraCore,
	velocity: *const f32,
	delta_time: f32,
	out_position: *mut f32,
	out_rotation: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let pose =
		unsafe { &mut *state }.view_model_pose(unsafe { &*camera }, vec3(velocity), delta_time);

	// SAFETY: guaranteed by the caller.
	unsafe {
		std::slice::from_raw_parts_mut(out_position, 3).copy_from_slice(&pose.position);
		std::slice::from_raw_parts_mut(out_rotation, 4).copy_from_slice(&pose.rotation);
	}
}
