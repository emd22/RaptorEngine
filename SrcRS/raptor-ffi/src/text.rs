use std::ffi::{CStr, c_char};

use raptor_math::mat4;
use raptor_render::text::{self, InstanceData, TextState};

pub type RxTextState = TextState;
pub type RxTextInstance = InstanceData;

pub const NO_ROOM: u32 = u32::MAX;

#[unsafe(no_mangle)]
pub extern "C" fn rx_text_glyph_width() -> u32
{
	text::GLYPH_WIDTH
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_text_glyph_height() -> u32
{
	text::GLYPH_HEIGHT
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_text_max_glyphs() -> u32
{
	text::MAX_GLYPHS as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_text_state_new() -> *mut RxTextState
{
	Box::into_raw(Box::new(TextState::default()))
}

/// # Safety
///
/// `state` must be null or come from `rx_text_state_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_state_free(state: *mut RxTextState)
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
pub unsafe extern "C" fn rx_text_begin_frame_if_needed(state: *mut RxTextState, frame_number: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.begin_frame_if_needed(frame_number);
}

/// # Safety
///
/// `state` must be live and the outputs writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_cursor(state: *const RxTextState, out_x: *mut f32, out_y: *mut f32)
{
	// SAFETY: guaranteed by the caller.
	let cursor = unsafe { &*state }.cursor();

	// SAFETY: guaranteed by the caller.
	unsafe {
		*out_x = cursor[0];
		*out_y = cursor[1];
	}
}

/// # Safety
///
/// `state` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_move_cursor_down(state: *mut RxTextState, amount: f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.move_cursor_down(amount);
}

/// Takes room for `count` quads in the frame's instance buffer and returns where they start in
/// bytes, or `u32::MAX` if they do not fit.
///
/// # Safety
///
/// `state` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_reserve(state: *mut RxTextState, count: u32) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *state }.reserve(count).unwrap_or(NO_ROOM)
}

/// Lays a NUL terminated string out from `origin_x`, `origin_y` into at most `capacity` quads, and
/// returns how many were written. `out_line_height` is the height of a line.
///
/// # Safety
///
/// `text` must be NUL terminated, `out_instances` valid for `capacity` instances and
/// `out_line_height` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_layout(
	text: *const c_char,
	scale: f32,
	origin_x: f32,
	origin_y: f32,
	window_width: u32,
	window_height: u32,
	atlas_width: u32,
	atlas_height: u32,
	out_instances: *mut RxTextInstance,
	capacity: usize,
	out_line_height: *mut f32,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let text = unsafe { CStr::from_ptr(text) }.to_bytes();

	let (instances, line_height) = text::layout_text(
		text,
		scale,
		[origin_x, origin_y],
		(window_width, window_height),
		(atlas_width, atlas_height),
	);

	let written = instances.len().min(capacity);

	// SAFETY: guaranteed by the caller.
	unsafe {
		std::ptr::copy_nonoverlapping(instances.as_ptr(), out_instances, written);
		*out_line_height = line_height;
	}

	written
}

/// # Safety
///
/// `out_instance` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_image_instance(
	x: f32,
	y: f32,
	width: f32,
	height: f32,
	window_width: u32,
	window_height: u32,
	out_instance: *mut RxTextInstance,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		*out_instance = text::image_instance([x, y], [width, height], (window_width, window_height))
	};
}

/// # Safety
///
/// `out` must be valid for 16 floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_text_ortho(width: f32, height: f32, near: f32, far: f32, out: *mut f32)
{
	let flat = mat4::flatten(&mat4::orthographic(width, height, near, far));

	// SAFETY: guaranteed by the caller.
	unsafe { std::ptr::copy_nonoverlapping(flat.as_ptr(), out, 16) };
}
