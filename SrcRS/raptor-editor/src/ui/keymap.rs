use wxdragon::keycode::*;

use crate::key::Key;

pub fn convert_key(code: i32) -> Key {
	match code {
		65..=90 => Key::A.offset(code - 65),
		97..=122 => Key::A.offset(code - 97),
		49..=57 => Key::Num1.offset(code - 49),
		WXK_F1..=WXK_F12 => Key::F1.offset(code - WXK_F1),
		WXK_NUMPAD1..=WXK_NUMPAD9 => Key::Numpad1.offset(code - WXK_NUMPAD1),
		48 | 41 => Key::Num0,
		33 => Key::Num1,
		64 => Key::Num2,
		35 => Key::Num3,
		36 => Key::Num4,
		37 => Key::Num5,
		94 => Key::Num6,
		38 => Key::Num7,
		42 => Key::Num8,
		40 => Key::Num9,
		WXK_RETURN => Key::Return,
		WXK_ESCAPE => Key::Escape,
		WXK_BACK => Key::Backspace,
		WXK_TAB => Key::Tab,
		WXK_SPACE => Key::Space,
		45 | 95 => Key::Minus,
		61 | 43 => Key::Equals,
		91 | 123 => Key::Lbracket,
		93 | 125 => Key::Rbracket,
		92 | 124 => Key::Backslash,
		59 | 58 => Key::Semicolon,
		39 | 34 => Key::Apostrophe,
		96 | 126 => Key::Grave,
		44 | 60 => Key::Comma,
		46 | 62 => Key::Period,
		47 | 63 => Key::Slash,
		WXK_CAPITAL => Key::Capslock,
		WXK_INSERT => Key::Insert,
		WXK_HOME => Key::Home,
		WXK_PAGEUP => Key::Pageup,
		WXK_DELETE => Key::Delete,
		WXK_END => Key::End,
		WXK_PAGEDOWN => Key::Pagedown,
		WXK_RIGHT => Key::Right,
		WXK_LEFT => Key::Left,
		WXK_DOWN => Key::Down,
		WXK_UP => Key::Up,
		WXK_NUMLOCK => Key::Numlock,
		WXK_NUMPAD_DIVIDE => Key::NumpadDivide,
		WXK_NUMPAD_MULTIPLY => Key::NumpadMultiply,
		WXK_NUMPAD_SUBTRACT => Key::NumpadMinus,
		WXK_NUMPAD_ADD => Key::NumpadPlus,
		WXK_NUMPAD_ENTER => Key::NumpadEnter,
		WXK_NUMPAD0 => Key::Numpad0,
		WXK_NUMPAD_DECIMAL => Key::NumpadPeriod,
		WXK_SHIFT => Key::Lshift,
		WXK_ALT => Key::Lalt,
		#[cfg(target_os = "macos")]
		WXK_CONTROL => Key::Lmeta,
		#[cfg(target_os = "macos")]
		WXK_RAW_CONTROL => Key::Lctrl,
		#[cfg(not(target_os = "macos"))]
		WXK_CONTROL => Key::Lctrl,
		#[cfg(not(target_os = "macos"))]
		WXK_WINDOWS_LEFT => Key::Lmeta,
		#[cfg(not(target_os = "macos"))]
		WXK_WINDOWS_RIGHT => Key::Rmeta,
		_ => Key::Unknown,
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn letters_and_digits_map_to_their_scancodes() {
		assert_eq!(convert_key(b'A' as i32), Key::A);
		assert_eq!(convert_key(b'z' as i32), Key::Z);
		assert_eq!(convert_key(b'1' as i32), Key::Num1);
		assert_eq!(convert_key(b'0' as i32), Key::Num0);
		assert_eq!(convert_key(WXK_F3), Key::F3);
		assert_eq!(convert_key(WXK_LEFT), Key::Left);
		assert_eq!(convert_key(-5), Key::Unknown);
	}
}
