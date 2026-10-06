#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[repr(u16)]
pub enum Key
{
	Unknown = 0,
	KeyA = 4,
	KeyB = 5,
	KeyC = 6,
	KeyD = 7,
	KeyE = 8,
	KeyF = 9,
	KeyG = 10,
	KeyH = 11,
	KeyI = 12,
	KeyJ = 13,
	KeyK = 14,
	KeyL = 15,
	KeyM = 16,
	KeyN = 17,
	KeyO = 18,
	KeyP = 19,
	KeyQ = 20,
	KeyR = 21,
	KeyS = 22,
	KeyT = 23,
	KeyU = 24,
	KeyV = 25,
	KeyW = 26,
	KeyX = 27,
	KeyY = 28,
	KeyZ = 29,
	Key1 = 30,
	Key2 = 31,
	Key3 = 32,
	Key4 = 33,
	Key5 = 34,
	Key6 = 35,
	Key7 = 36,
	Key8 = 37,
	Key9 = 38,
	Key0 = 39,
	KeyReturn = 40,
	KeyEscape = 41,
	KeyBackspace = 42,
	KeyTab = 43,
	KeySpace = 44,
	KeyMinus = 45,
	KeyEquals = 46,
	KeyLbracket = 47,
	KeyRbracket = 48,
	KeyBackslash = 49,
	KeySemicolon = 51,
	KeyApostrophe = 52,
	KeyGrave = 53,
	KeyComma = 54,
	KeyPeriod = 55,
	KeySlash = 56,
	KeyCapslock = 57,
	KeyF1 = 58,
	KeyF2 = 59,
	KeyF3 = 60,
	KeyF4 = 61,
	KeyF5 = 62,
	KeyF6 = 63,
	KeyF7 = 64,
	KeyF8 = 65,
	KeyF9 = 66,
	KeyF10 = 67,
	KeyF11 = 68,
	KeyF12 = 69,
	KeyPrintscreen = 70,
	KeyScrolllock = 71,
	KeyPause = 72,
	KeyInsert = 73,
	KeyHome = 74,
	KeyPageup = 75,
	KeyDelete = 76,
	KeyEnd = 77,
	KeyPagedown = 78,
	KeyRight = 79,
	KeyLeft = 80,
	KeyDown = 81,
	KeyUp = 82,
	KeyNumlock = 83,
	KeyNumpadDivide = 84,
	KeyNumpadMultiply = 85,
	KeyNumpadMinus = 86,
	KeyNumpadPlus = 87,
	KeyNumpadEnter = 88,
	KeyNumpad1 = 89,
	KeyNumpad2 = 90,
	KeyNumpad3 = 91,
	KeyNumpad4 = 92,
	KeyNumpad5 = 93,
	KeyNumpad6 = 94,
	KeyNumpad7 = 95,
	KeyNumpad8 = 96,
	KeyNumpad9 = 97,
	KeyNumpad0 = 98,
	KeyNumpadPeriod = 99,
	KeyLctrl = 100,
	KeyLshift = 101,
	KeyLalt = 102,
	KeyLmeta = 103,
	KeyRctrl = 104,
	KeyRshift = 105,
	KeyRalt = 106,
	KeyRmeta = 107,
	MouseButtonsStart = 108,
	MouseLeft = 109,
	MouseMiddle = 110,
	MouseRight = 111,
}

impl Key
{
	pub const KEY_TILDE: Key = Key::KeyGrave;
	pub const KEY_MAC_HELP: Key = Key::KeyInsert;
	pub const KEY_MAC_CLEAR: Key = Key::KeyNumlock;
	pub const MOUSE_UNUSED: Key = Key::MouseButtonsStart;

	pub const KEYBOARD_BEGIN: u16 = 4;
	pub const KEYBOARD_STANDARD_END: u16 = 99;
	pub const KEYBOARD_MODIFIERS_START: u16 = 100;
	pub const KEYBOARD_END: u16 = 107;
	pub const MOUSE_BUTTONS_START: u16 = 108;
	pub const MODIFIERS_OFFSET: u16 = 124;
	pub const MAX: u16 = 113;

	pub const fn code(self) -> u16
	{
		self as u16
	}

	pub fn from_code(code: u16) -> Key
	{
		KEYS.iter()
			.copied()
			.find(|key| key.code() == code)
			.unwrap_or(Key::Unknown)
	}

	pub fn from_scancode(scancode: u32) -> Key
	{
		let code = match scancode {
			224..=231 => scancode - 124,
			_ => scancode,
		};

		match u16::try_from(code) {
			Ok(code) if (Key::KEYBOARD_BEGIN..=Key::KEYBOARD_END).contains(&code) => {
				Key::from_code(code)
			}
			_ => Key::Unknown,
		}
	}

	pub fn from_mouse_button(button: u8) -> Key
	{
		match button {
			1 => Key::MouseLeft,
			2 => Key::MouseMiddle,
			3 => Key::MouseRight,
			_ => Key::Unknown,
		}
	}

	pub const fn is_mouse(self) -> bool
	{
		self.code() >= Key::MOUSE_BUTTONS_START
	}

	pub const fn is_modifier(self) -> bool
	{
		self.code() >= Key::KEYBOARD_MODIFIERS_START && self.code() <= Key::KEYBOARD_END
	}

	pub const fn index(self) -> usize
	{
		self as usize
	}
}

const KEYS: &[Key] = &[
	Key::KeyA,
	Key::KeyB,
	Key::KeyC,
	Key::KeyD,
	Key::KeyE,
	Key::KeyF,
	Key::KeyG,
	Key::KeyH,
	Key::KeyI,
	Key::KeyJ,
	Key::KeyK,
	Key::KeyL,
	Key::KeyM,
	Key::KeyN,
	Key::KeyO,
	Key::KeyP,
	Key::KeyQ,
	Key::KeyR,
	Key::KeyS,
	Key::KeyT,
	Key::KeyU,
	Key::KeyV,
	Key::KeyW,
	Key::KeyX,
	Key::KeyY,
	Key::KeyZ,
	Key::Key1,
	Key::Key2,
	Key::Key3,
	Key::Key4,
	Key::Key5,
	Key::Key6,
	Key::Key7,
	Key::Key8,
	Key::Key9,
	Key::Key0,
	Key::KeyReturn,
	Key::KeyEscape,
	Key::KeyBackspace,
	Key::KeyTab,
	Key::KeySpace,
	Key::KeyMinus,
	Key::KeyEquals,
	Key::KeyLbracket,
	Key::KeyRbracket,
	Key::KeyBackslash,
	Key::KeySemicolon,
	Key::KeyApostrophe,
	Key::KeyGrave,
	Key::KeyComma,
	Key::KeyPeriod,
	Key::KeySlash,
	Key::KeyCapslock,
	Key::KeyF1,
	Key::KeyF2,
	Key::KeyF3,
	Key::KeyF4,
	Key::KeyF5,
	Key::KeyF6,
	Key::KeyF7,
	Key::KeyF8,
	Key::KeyF9,
	Key::KeyF10,
	Key::KeyF11,
	Key::KeyF12,
	Key::KeyPrintscreen,
	Key::KeyScrolllock,
	Key::KeyPause,
	Key::KeyInsert,
	Key::KeyHome,
	Key::KeyPageup,
	Key::KeyDelete,
	Key::KeyEnd,
	Key::KeyPagedown,
	Key::KeyRight,
	Key::KeyLeft,
	Key::KeyDown,
	Key::KeyUp,
	Key::KeyNumlock,
	Key::KeyNumpadDivide,
	Key::KeyNumpadMultiply,
	Key::KeyNumpadMinus,
	Key::KeyNumpadPlus,
	Key::KeyNumpadEnter,
	Key::KeyNumpad1,
	Key::KeyNumpad2,
	Key::KeyNumpad3,
	Key::KeyNumpad4,
	Key::KeyNumpad5,
	Key::KeyNumpad6,
	Key::KeyNumpad7,
	Key::KeyNumpad8,
	Key::KeyNumpad9,
	Key::KeyNumpad0,
	Key::KeyNumpadPeriod,
	Key::KeyLctrl,
	Key::KeyLshift,
	Key::KeyLalt,
	Key::KeyLmeta,
	Key::KeyRctrl,
	Key::KeyRshift,
	Key::KeyRalt,
	Key::KeyRmeta,
	Key::MouseButtonsStart,
	Key::MouseLeft,
	Key::MouseMiddle,
	Key::MouseRight,
];

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn scancodes_map_to_keys()
	{
		assert_eq!(Key::from_scancode(4), Key::KeyA);
		assert_eq!(Key::from_scancode(224), Key::KeyLctrl);
		assert_eq!(Key::from_scancode(231), Key::KeyRmeta);
		assert_eq!(Key::from_scancode(50), Key::Unknown);
		assert_eq!(Key::from_scancode(500), Key::Unknown);
		assert_eq!(Key::KEY_TILDE, Key::KeyGrave);
		assert_eq!(Key::from_mouse_button(3), Key::MouseRight);
		assert!(Key::MouseLeft.is_mouse());
		assert!(Key::KeyLshift.is_modifier());
	}
}
