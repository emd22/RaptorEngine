use std::cell::Cell;

use raptor_core::Key;

const KEY_COUNT: usize = Key::MAX as usize;

#[derive(Clone, Copy, Default)]
struct Control {
	down: bool,
	continued: bool,
	tick_bit: bool,
}

pub struct Controls {
	keys: [Cell<Control>; KEY_COUNT],
	tick: bool,
	mouse_delta: [f32; 2],
	mouse_captured: bool,
	captured_mouse_position: (f32, f32),
}

impl Default for Controls {
	fn default() -> Self {
		Self {
			keys: [const { Cell::new(Control { down: false, continued: false, tick_bit: false }) }; KEY_COUNT],
			tick: false,
			mouse_delta: [0.0; 2],
			mouse_captured: false,
			captured_mouse_position: (0.0, 0.0),
		}
	}
}

impl Controls {
	pub fn new() -> Self {
		Self::default()
	}

	/// Starts a new frame: the mouse delta is cleared and keys that stay down stop counting as pressed.
	pub fn begin_frame(&mut self) {
		self.mouse_delta = [0.0; 2];
		self.tick = !self.tick;
	}

	fn settle(&self, key: Key) {
		let tick = self.tick;
		let mut control = self.keys[key.index()].get();

		if control.down && !control.continued && control.tick_bit != tick {
			control.continued = true;
			self.keys[key.index()].set(control);
		}
	}

	pub fn post_button(&mut self, key: Key, down: bool) {
		let tick = self.tick;

		let Some(cell) = self.keys.get(key.index()).filter(|_| key != Key::Unknown) else {
			return;
		};

		let mut control = cell.get();

		if down && !control.down {
			control.tick_bit = tick;
			control.down = true;
		} else if !down && control.down {
			control.down = false;
			control.continued = false;
		}

		cell.set(control);
	}

	pub fn post_mouse_motion(&mut self, delta: [f32; 2]) {
		self.mouse_delta[0] += delta[0];
		self.mouse_delta[1] += delta[1];
	}

	pub fn is_down(&self, key: Key) -> bool {
		if key.index() >= KEY_COUNT {
			return false;
		}

		self.settle(key);

		self.keys[key.index()].get().down
	}

	pub fn is_up(&self, key: Key) -> bool {
		self.keys
			.get(key.index())
			.is_none_or(|control| !control.get().down)
	}

	pub fn is_pressed(&self, key: Key) -> bool {
		if key.index() >= KEY_COUNT {
			return false;
		}

		self.settle(key);

		let control = self.keys[key.index()].get();

		control.down && !control.continued
	}

	pub fn combo_down(&self, keys: &[Key]) -> bool {
		keys.iter().all(|key| self.is_down(*key))
	}

	pub fn combo_pressed(&self, keys: &[Key]) -> bool {
		let all_down = keys.iter().all(|key| self.is_down(*key));

		all_down && keys.iter().any(|key| self.is_pressed(*key))
	}

	pub fn reset_key(&mut self, key: Key) {
		if let Some(control) = self.keys.get(key.index()) {
			control.set(Control::default());
		}
	}

	pub fn release_all(&mut self) {
		for index in 0..KEY_COUNT {
			self.post_button(Key::from_code(index as u16), false);
		}
	}

	pub fn release_non_modifiers(&mut self) {
		for code in Key::KEYBOARD_BEGIN..=Key::KEYBOARD_STANDARD_END {
			self.post_button(Key::from_code(code), false);
		}
	}

	pub fn mouse_delta(&self) -> [f32; 2] {
		self.mouse_delta
	}

	pub fn mouse_captured(&self) -> bool {
		self.mouse_captured
	}

	pub fn set_mouse_captured(&mut self, captured: bool, position: (f32, f32)) {
		if captured {
			self.captured_mouse_position = position;
		}

		self.mouse_captured = captured;
	}

	pub fn captured_mouse_position(&self) -> (f32, f32) {
		self.captured_mouse_position
	}

	/// The character typed by a key pressed this frame, with shift picking capitals and symbols.
	pub fn typed_char(&self) -> Option<char> {
		let shift = self.is_down(Key::KeyLshift);

		for code in Key::KeyA.code()..=Key::KeyZ.code() {
			if self.is_pressed(Key::from_code(code)) {
				let offset = (code - Key::KeyA.code()) as u8;

				return Some(if shift { b'A' + offset } else { b'a' + offset } as char);
			}
		}

		if shift && self.is_pressed(Key::KeyMinus) {
			return Some('_');
		}

		const SHIFTED: [char; 10] = ['!', '@', '#', '$', '%', '^', '&', '*', '(', ')'];

		for code in Key::Key1.code()..=Key::Key0.code() {
			if self.is_pressed(Key::from_code(code)) {
				let index = (code - Key::Key1.code()) as usize;

				return Some(if shift {
					SHIFTED[index]
				} else if index == 9 {
					'0'
				} else {
					(b'1' + index as u8) as char
				});
			}
		}

		if self.is_pressed(Key::KeySpace) {
			return Some(' ');
		}

		if self.is_pressed(Key::KeyReturn) {
			return Some('\n');
		}

		None
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn a_key_is_pressed_only_on_the_frame_it_went_down() {
		let mut controls = Controls::new();

		controls.begin_frame();
		controls.post_button(Key::KeyW, true);

		assert!(controls.is_down(Key::KeyW));
		assert!(controls.is_pressed(Key::KeyW));

		controls.begin_frame();

		assert!(controls.is_down(Key::KeyW));
		assert!(!controls.is_pressed(Key::KeyW));

		controls.post_button(Key::KeyW, false);

		assert!(controls.is_up(Key::KeyW));
	}

	#[test]
	fn mouse_motion_adds_up_within_a_frame() {
		let mut controls = Controls::new();

		controls.begin_frame();
		controls.post_mouse_motion([1.0, 2.0]);
		controls.post_mouse_motion([3.0, 4.0]);

		assert_eq!(controls.mouse_delta(), [4.0, 6.0]);

		controls.begin_frame();

		assert_eq!(controls.mouse_delta(), [0.0, 0.0]);
	}

	#[test]
	fn shift_picks_capitals_and_symbols() {
		let mut controls = Controls::new();

		controls.begin_frame();
		controls.post_button(Key::KeyLshift, true);
		controls.post_button(Key::Key1, true);

		assert_eq!(controls.typed_char(), Some('!'));

		controls.begin_frame();
		controls.post_button(Key::Key1, false);
		controls.post_button(Key::KeyLshift, false);
		controls.post_button(Key::KeyQ, true);

		assert_eq!(controls.typed_char(), Some('q'));
	}

	#[test]
	fn combos_need_every_key_and_a_fresh_press() {
		let mut controls = Controls::new();

		controls.begin_frame();
		controls.post_button(Key::KeyLctrl, true);

		assert!(!controls.combo_pressed(&[Key::KeyLctrl, Key::KeyS]));

		controls.begin_frame();
		controls.post_button(Key::KeyS, true);

		assert!(controls.combo_pressed(&[Key::KeyLctrl, Key::KeyS]));
	}
}
