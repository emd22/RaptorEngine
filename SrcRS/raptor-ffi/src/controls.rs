use std::sync::{Mutex, MutexGuard, PoisonError};

use raptor_core::Key;
use raptor_input::{Controls, WindowEvent, pump_sdl};

pub const RX_WINDOW_EVENT_QUIT: u32 = 1;
pub const RX_WINDOW_EVENT_RESIZED: u32 = 2;

static CONTROLS: Mutex<Option<Controls>> = Mutex::new(None);

fn with<R>(f: impl FnOnce(&mut Controls) -> R) -> R
{
	let mut guard: MutexGuard<Option<Controls>> =
		CONTROLS.lock().unwrap_or_else(PoisonError::into_inner);

	f(guard.get_or_insert_with(Controls::new))
}

fn key(code: u32) -> Key
{
	Key::from_code(u16::try_from(code).unwrap_or(0))
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_begin_frame()
{
	with(Controls::begin_frame);
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_pump_sdl() -> u32
{
	with(|controls| {
		pump_sdl(controls)
			.into_iter()
			.fold(0, |flags, event| match event {
				WindowEvent::Quit => flags | RX_WINDOW_EVENT_QUIT,
				WindowEvent::Resized => flags | RX_WINDOW_EVENT_RESIZED,
			})
	})
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_is_down(code: u32) -> bool
{
	with(|controls| controls.is_down(key(code)))
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_is_up(code: u32) -> bool
{
	with(|controls| controls.is_up(key(code)))
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_is_pressed(code: u32) -> bool
{
	with(|controls| controls.is_pressed(key(code)))
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_reset_key(code: u32)
{
	with(|controls| controls.reset_key(key(code)));
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_post_button(code: u32, down: bool)
{
	with(|controls| controls.post_button(key(code), down));
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_post_mouse_motion(x: f32, y: f32)
{
	with(|controls| controls.post_mouse_motion([x, y]));
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_release_all()
{
	with(Controls::release_all);
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_release_non_modifiers()
{
	with(Controls::release_non_modifiers);
}

/// # Safety
///
/// `out` must be writable for two floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_controls_mouse_delta(out: *mut f32)
{
	let delta = with(|controls| controls.mouse_delta());

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts_mut(out, 2) }.copy_from_slice(&delta);
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_set_mouse_captured(captured: bool, x: f32, y: f32)
{
	with(|controls| controls.set_mouse_captured(captured, (x, y)));
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_mouse_captured() -> bool
{
	with(|controls| controls.mouse_captured())
}

/// # Safety
///
/// `out` must be writable for two floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_controls_captured_mouse_position(out: *mut f32)
{
	let position = with(|controls| controls.captured_mouse_position());

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts_mut(out, 2) }.copy_from_slice(&[position.0, position.1]);
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_controls_typed_char() -> u32
{
	with(|controls| controls.typed_char()).map_or(0, u32::from)
}
