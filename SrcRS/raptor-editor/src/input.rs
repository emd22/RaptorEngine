use std::sync::{Arc, Mutex, MutexGuard};

use crate::key::Key;

#[derive(Clone, Debug)]
pub struct InputState {
	down: [bool; Key::COUNT],
	pressed: [bool; Key::COUNT],
	released: [bool; Key::COUNT],
	mouse_delta: (f32, f32),
}

impl Default for InputState {
	fn default() -> Self {
		Self {
			down: [false; Key::COUNT],
			pressed: [false; Key::COUNT],
			released: [false; Key::COUNT],
			mouse_delta: (0.0, 0.0),
		}
	}
}

impl InputState {
	pub fn post_button(&mut self, key: Key, is_down: bool) {
		let index = key.index();

		if key == Key::Unknown || index >= Key::COUNT {
			return;
		}

		if is_down && !self.down[index] {
			self.pressed[index] = true;
		}

		if !is_down && self.down[index] {
			self.released[index] = true;
		}

		self.down[index] = is_down;
	}

	pub fn post_motion(&mut self, dx: f32, dy: f32) {
		self.mouse_delta.0 += dx;
		self.mouse_delta.1 += dy;
	}

	pub fn release_all(&mut self) {
		for index in 0..Key::COUNT {
			if self.down[index] {
				self.released[index] = true;
			}
		}

		self.down = [false; Key::COUNT];
	}

	pub fn release_non_modifiers(&mut self) {
		for index in 0..Key::COUNT {
			let is_modifier = (Key::Lctrl.index()..=Key::Rmeta.index()).contains(&index);

			if !is_modifier && self.down[index] {
				self.down[index] = false;
				self.released[index] = true;
			}
		}
	}

	pub fn key_down(&self, key: Key) -> bool {
		self.down.get(key.index()).copied().unwrap_or(false)
	}

	pub fn key_pressed(&self, key: Key) -> bool {
		self.pressed.get(key.index()).copied().unwrap_or(false)
	}

	pub fn key_released(&self, key: Key) -> bool {
		self.released.get(key.index()).copied().unwrap_or(false)
	}

	pub fn take_mouse_delta(&mut self) -> (f32, f32) {
		std::mem::take(&mut self.mouse_delta)
	}

	pub fn end_frame(&mut self) {
		self.pressed = [false; Key::COUNT];
		self.released = [false; Key::COUNT];
	}
}

#[derive(Clone, Default)]
pub struct SharedInput(Arc<Mutex<InputState>>);

impl SharedInput {
	pub fn lock(&self) -> MutexGuard<'_, InputState> {
		self.0
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn pressed_lasts_one_frame_and_down_persists() {
		let mut input = InputState::default();

		input.post_button(Key::W, true);
		input.post_button(Key::W, true);

		assert!(input.key_pressed(Key::W) && input.key_down(Key::W));

		input.end_frame();

		assert!(!input.key_pressed(Key::W) && input.key_down(Key::W));

		input.post_button(Key::W, false);

		assert!(input.key_released(Key::W) && !input.key_down(Key::W));
	}

	#[test]
	fn releasing_everything_lifts_held_keys() {
		let mut input = InputState::default();

		input.post_button(Key::MouseLeft, true);
		input.post_button(Key::Lctrl, true);
		input.release_non_modifiers();

		assert!(!input.key_down(Key::MouseLeft) && input.key_down(Key::Lctrl));

		input.release_all();

		assert!(!input.key_down(Key::Lctrl));
	}

	#[test]
	fn mouse_motion_accumulates_until_taken() {
		let mut input = InputState::default();

		input.post_motion(1.0, 2.0);
		input.post_motion(2.0, -1.0);

		assert_eq!(input.take_mouse_delta(), (3.0, 1.0));
		assert_eq!(input.take_mouse_delta(), (0.0, 0.0));
	}
}
