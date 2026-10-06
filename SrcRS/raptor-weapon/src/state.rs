#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum Phase {
	Ready = 0,
	Reloading = 1,
	Raising = 2,
	Lowering = 3,
	Lowered = 4,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum Event {
	Fire = 0,
	DryFire = 1,
	ReloadBegin = 2,
	ReloadEnd = 3,
	Raise = 4,
	Lower = 5,
	ModeChanged = 6,
}

impl Event {
	pub fn from_raw(raw: i32) -> Option<Self> {
		Some(match raw {
			0 => Self::Fire,
			1 => Self::DryFire,
			2 => Self::ReloadBegin,
			3 => Self::ReloadEnd,
			4 => Self::Raise,
			5 => Self::Lower,
			6 => Self::ModeChanged,
			_ => return None,
		})
	}

	pub fn name(self) -> &'static str {
		match self {
			Self::Fire => "fire",
			Self::DryFire => "dry fire",
			Self::ReloadBegin => "reload begin",
			Self::ReloadEnd => "reload end",
			Self::Raise => "raise",
			Self::Lower => "lower",
			Self::ModeChanged => "mode changed",
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FireMode {
	None,
	Semi,
	Auto,
	Burst,
}

impl FireMode {
	pub fn from_raw(raw: i32) -> Self {
		match raw {
			1 => Self::Semi,
			2 => Self::Auto,
			4 => Self::Burst,
			_ => Self::None,
		}
	}

	pub fn label(self) -> &'static str {
		match self {
			Self::Semi => "SEMI",
			Self::Auto => "AUTO",
			Self::Burst => "BURST",
			Self::None => "",
		}
	}
}

pub mod input_flags {
	pub const FIRE_PRESSED: i32 = 1 << 0;
	pub const FIRE_HELD: i32 = 1 << 1;
	pub const RELOAD_PRESSED: i32 = 1 << 2;
	pub const SWITCH_MODE: i32 = 1 << 3;
	pub const SWITCH_WEAPON: i32 = 1 << 4;
	pub const AIRBORNE: i32 = 1 << 5;
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct InputFlags(pub i32);

impl InputFlags {
	pub fn has(self, flag: i32) -> bool {
		self.0 & flag != 0
	}

	pub fn with(self, flag: i32, value: bool) -> Self {
		if value {
			Self(self.0 | flag)
		} else {
			Self(self.0 & !flag)
		}
	}
}

pub use input_flags::*;

/// What a weapon script keeps of its weapon between updates. The layout matches the script's struct.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ScriptState {
	pub phase: i32,
	pub fire_mode: i32,
	pub magazine: i32,
	pub reserve: i32,
	pub burst_remaining: i32,
	pub recoil_shots: i32,
	pub timer: f32,
	pub timer_total: f32,
	pub cooldown: f32,
	pub since_shot: f32,
	pub bloom: f32,
	pub fire_buffer: f32,
}

impl Default for ScriptState {
	fn default() -> Self {
		Self {
			phase: Phase::Ready as i32,
			fire_mode: 0,
			magazine: 0,
			reserve: 0,
			burst_remaining: 0,
			recoil_shots: 0,
			timer: 0.0,
			timer_total: 1.0,
			cooldown: 0.0,
			since_shot: 0.0,
			bloom: 0.0,
			fire_buffer: 0.0,
		}
	}
}

impl ScriptState {
	pub fn phase(&self) -> Phase {
		match self.phase {
			1 => Phase::Reloading,
			2 => Phase::Raising,
			3 => Phase::Lowering,
			4 => Phase::Lowered,
			_ => Phase::Ready,
		}
	}

	pub fn set_phase(&mut self, phase: Phase) {
		self.phase = phase as i32;
	}
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ScriptInput {
	pub flags: i32,
	pub move_amount: f32,
}

const _: () = assert!(size_of::<ScriptState>() == 12 * 4);
const _: () = assert!(size_of::<ScriptInput>() == 2 * 4);
