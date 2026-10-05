use raptor_config::host::Host;
use raptor_config::model::{Entry, Kind};

use crate::access::{Reader, find, member};

pub const MODE_NONE: i32 = 0;
pub const MODE_SEMI: i32 = 1;
pub const MODE_AUTO: i32 = 2;
pub const MODE_BURST: i32 = 4;

/// What a weapon script is given about its weapon. The order and the 4-byte fields match the
/// script's struct.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ScriptDef
{
	pub damage: f32,
	pub range: f32,
	pub rounds_per_minute: f32,
	pub pellets: i32,

	pub fire_mode_flags: i32,
	pub default_mode: i32,

	pub burst_count: i32,
	pub burst_rounds_per_minute: f32,
	pub burst_cooldown: f32,
	pub hit_force: f32,
	pub decals: i32,

	pub spread_hip: f32,
	pub spread_moving: f32,
	pub spread_air: f32,
	pub spread_per_shot: f32,
	pub spread_max: f32,
	pub spread_recovery: f32,

	pub recoil_pitch: f32,
	pub recoil_pitch_variance: f32,
	pub recoil_yaw: f32,
	pub recoil_ramp: f32,
	pub recoil_ramp_max: f32,
	pub recoil_recovery: f32,
	pub recoil_reset_time: f32,

	pub falloff_start: f32,
	pub falloff_end: f32,
	pub falloff_min: f32,

	pub magazine_size: i32,
	pub reserve_max: i32,
	pub reserve_start: i32,
	pub reload_time: f32,
	pub reload_empty_time: f32,
	pub auto_reload: i32,

	pub raise_time: f32,
	pub lower_time: f32,
}

const _: () = assert!(size_of::<ScriptDef>() == 35 * 4);

#[derive(Clone, Debug, Default, PartialEq)]
pub struct WeaponDef
{
	pub name: String,
	pub slot: i32,
	pub script: String,
	pub idle_anim: String,
	pub fire_anim: String,
	pub reload_anim: String,
	/// The camera kick of a shot, in degrees
	pub view_kick: f32,
	pub def: ScriptDef,
}

#[derive(Debug, PartialEq, Eq)]
pub enum WeaponError
{
	Parse,
	NoScript,
}

fn mode_from_name(name: &[u8]) -> i32
{
	if name.eq_ignore_ascii_case(b"semi") {
		MODE_SEMI
	}
	else if name.eq_ignore_ascii_case(b"auto") {
		MODE_AUTO
	}
	else if name.eq_ignore_ascii_case(b"burst") {
		MODE_BURST
	}
	else {
		MODE_NONE
	}
}

/// The modes a weapon has and the first of them, which it starts in. A weapon with none is semi-auto.
fn parse_modes(reader: &mut Reader, fire: Option<&Entry>) -> (i32, i32)
{
	let mut flags = MODE_NONE;
	let mut default = MODE_NONE;

	if let Some(entry) = fire.and_then(|fire| member(fire, "modes"))
		&& entry.is_array
	{
		for primitive in &entry.array {
			let name = if primitive.kind == Kind::String {
				primitive.string_value.as_deref().unwrap_or(&[])
			}
			else {
				reader.warn("Attempting to retrieve string from non-string config primitive");
				&[]
			};

			let mode = mode_from_name(name);

			flags |= mode;

			if default == MODE_NONE {
				default = mode;
			}
		}
	}

	if flags == MODE_NONE {
		(MODE_SEMI, MODE_SEMI)
	}
	else {
		(flags, default)
	}
}

fn text(reader: &mut Reader, parent: Option<&Entry>, name: &str, fallback: &str) -> String
{
	let Some(entry) = parent.and_then(|parent| member(parent, name)) else {
		return fallback.to_owned();
	};

	entry_text(reader, entry)
}

fn entry_text(reader: &mut Reader, entry: &Entry) -> String
{
	if entry.value.kind != Kind::String {
		reader.warn("Attempting to retrieve string from non-string config primitive");
		return String::new();
	}

	String::from_utf8_lossy(entry.value.string_value.as_deref().unwrap_or(&[])).into_owned()
}

fn float(reader: &mut Reader, parent: Option<&Entry>, name: &str, fallback: f32) -> f32
{
	parent
		.and_then(|parent| reader.float(parent, name))
		.unwrap_or(fallback)
}

fn int(reader: &mut Reader, parent: Option<&Entry>, name: &str, fallback: i32) -> i32
{
	parent
		.and_then(|parent| reader.int(parent, name))
		.map_or(fallback, |value| value as i32)
}

impl WeaponDef
{
	pub fn from_entries(
		entries: &[Entry],
		has_errors: bool,
		host: &mut dyn Host,
	) -> Result<Self, WeaponError>
	{
		if has_errors {
			return Err(WeaponError::Parse);
		}

		let mut reader = Reader::new(host);

		let script = find(entries, "script").ok_or(WeaponError::NoScript)?;

		let name = find(entries, "name");
		let slot = find(entries, "slot");
		let animations = find(entries, "animations");
		let fire = find(entries, "fire");
		let spread = find(entries, "spread");
		let recoil = find(entries, "recoil");
		let falloff = find(entries, "falloff");
		let ammo = find(entries, "ammo");
		let switching = find(entries, "switch");

		let reader = &mut reader;

		let script = entry_text(reader, script);
		let name = name.map_or_else(|| "Weapon".to_owned(), |name| entry_text(reader, name));
		let slot = slot.map_or(0, |slot| reader.int_of(&slot.value) as i32);

		let mut def = ScriptDef {
			damage: float(reader, fire, "damage", 20.0),
			range: float(reader, fire, "range", 100.0),
			rounds_per_minute: float(reader, fire, "rounds_per_minute", 600.0),
			pellets: int(reader, fire, "pellets", 1).max(1),
			burst_count: int(reader, fire, "burst_count", 3).max(1),
			burst_cooldown: float(reader, fire, "burst_cooldown", 0.0),
			hit_force: float(reader, fire, "hit_force", 0.0),
			decals: int(reader, fire, "decals", 1),

			spread_hip: float(reader, spread, "hip", 0.0),
			spread_moving: float(reader, spread, "moving", 0.0),
			spread_air: float(reader, spread, "air", 0.0),
			spread_per_shot: float(reader, spread, "per_shot", 0.0),
			spread_max: float(reader, spread, "max", 0.0),
			spread_recovery: float(reader, spread, "recovery", 0.0),

			recoil_pitch: float(reader, recoil, "pitch", 0.0),
			recoil_pitch_variance: float(reader, recoil, "pitch_variance", 0.0),
			recoil_yaw: float(reader, recoil, "yaw", 0.0),
			recoil_ramp: float(reader, recoil, "ramp", 0.0),
			recoil_ramp_max: float(reader, recoil, "ramp_max", 0.0),
			recoil_recovery: float(reader, recoil, "recovery", 0.0),
			recoil_reset_time: float(reader, recoil, "reset_time", 0.25),

			magazine_size: int(reader, ammo, "magazine_size", 1).max(1),
			reserve_max: int(reader, ammo, "reserve_max", 0).max(0),
			reload_time: float(reader, ammo, "reload_time", 2.0),
			auto_reload: int(reader, ammo, "auto_reload", 1),

			raise_time: float(reader, switching, "raise_time", 0.4),
			lower_time: float(reader, switching, "lower_time", 0.3),

			..ScriptDef::default()
		};

		(def.fire_mode_flags, def.default_mode) = parse_modes(reader, fire);

		def.burst_rounds_per_minute =
			float(reader, fire, "burst_rounds_per_minute", def.rounds_per_minute);

		def.falloff_start = float(reader, falloff, "start", def.range);
		def.falloff_end = float(reader, falloff, "end", def.range);
		def.falloff_min = float(reader, falloff, "min_multiplier", 1.0);

		def.reserve_start = int(reader, ammo, "reserve_start", def.reserve_max).clamp(0, def.reserve_max);
		def.reload_empty_time = float(reader, ammo, "reload_empty_time", def.reload_time);

		Ok(Self {
			name,
			slot,
			script,
			idle_anim: text(reader, animations, "idle", "BASE"),
			fire_anim: text(reader, animations, "fire", "Armature|Fire"),
			reload_anim: text(reader, animations, "reload", "Armature|ReloadClip"),
			view_kick: float(reader, recoil, "view_kick", 0.25),
			def,
		})
	}

	pub fn parse(
		data: &[u8],
		prelude_path: Option<&[u8]>,
		host: &mut dyn Host,
	) -> Result<Self, WeaponError>
	{
		let parsed = raptor_config::parse(data, prelude_path, b".conf", host);

		Self::from_entries(&parsed.entries, parsed.has_errors, host)
	}
}

#[cfg(test)]
mod tests
{
	use super::*;
	use raptor_config::host::LogLevel;

	#[derive(Default)]
	struct TestHost
	{
		messages: Vec<String>,
	}

	impl Host for TestHost
	{
		fn read_include(&mut self, _path: &[u8], _extension: &[u8]) -> Option<Vec<u8>>
		{
			Some(b"true = 1\nfalse = 0\n".to_vec())
		}

		fn log(&mut self, _level: LogLevel, _category: i32, message: &[u8])
		{
			self.messages
				.push(String::from_utf8_lossy(message).into_owned());
		}
	}

	fn parse(text: &str) -> (Result<WeaponDef, WeaponError>, TestHost)
	{
		let mut host = TestHost::default();
		let weapon = WeaponDef::parse(text.as_bytes(), Some(b"constants"), &mut host);

		(weapon, host)
	}

	#[test]
	fn a_full_file_reads_every_value()
	{
		let (weapon, host) = parse(
			"name = \"RIFLE\"\nslot = 2\nscript = \"Scripts/w.strata\"\nanimations = {\n idle = \"I\"\n fire = \"F\"\n}\nfire = {\n damage = 24.0\n pellets = 0\n modes = [\"auto\", \"Semi\", \"Burst\"]\n burst_count = 3\n decals = $true\n}\nrecoil = {\n view_kick = 2.5\n recovery = 7.0\n}\nfalloff = {\n start = 40.0\n}\nammo = {\n magazine_size = 30\n reserve_max = 180\n reserve_start = 999\n reload_time = 1.9\n}\nswitch = {\n raise_time = 0.45\n}\n",
		);

		let weapon = weapon.unwrap();

		assert!(host.messages.is_empty());
		assert_eq!(weapon.name, "RIFLE");
		assert_eq!(weapon.slot, 2);
		assert_eq!(weapon.script, "Scripts/w.strata");
		assert_eq!(weapon.idle_anim, "I");
		assert_eq!(weapon.fire_anim, "F");
		assert_eq!(weapon.reload_anim, "Armature|ReloadClip");
		assert_eq!(weapon.view_kick, 2.5);
		assert_eq!(weapon.def.damage, 24.0);
		assert_eq!(weapon.def.range, 100.0);
		assert_eq!(weapon.def.pellets, 1);
		assert_eq!(weapon.def.fire_mode_flags, MODE_AUTO | MODE_SEMI | MODE_BURST);
		assert_eq!(weapon.def.default_mode, MODE_AUTO);
		assert_eq!(weapon.def.burst_rounds_per_minute, 600.0);
		assert_eq!(weapon.def.decals, 1);
		assert_eq!(weapon.def.recoil_reset_time, 0.25);
		assert_eq!(weapon.def.recoil_recovery, 7.0);
		assert_eq!(weapon.def.falloff_start, 40.0);
		assert_eq!(weapon.def.falloff_end, 100.0);
		assert_eq!(weapon.def.reserve_start, 180);
		assert_eq!(weapon.def.reload_empty_time, 1.9);
		assert_eq!(weapon.def.raise_time, 0.45);
		assert_eq!(weapon.def.lower_time, 0.3);
	}

	#[test]
	fn defaults_apply_to_a_bare_file()
	{
		let (weapon, _) = parse("script = \"s\"\n");
		let weapon = weapon.unwrap();

		assert_eq!(weapon.name, "Weapon");
		assert_eq!(weapon.idle_anim, "BASE");
		assert_eq!(weapon.view_kick, 0.25);
		assert_eq!(weapon.def.fire_mode_flags, MODE_SEMI);
		assert_eq!(weapon.def.default_mode, MODE_SEMI);
		assert_eq!(weapon.def.magazine_size, 1);
		assert_eq!(weapon.def.reload_time, 2.0);
		assert_eq!(weapon.def.reload_empty_time, 2.0);
		assert_eq!(weapon.def.auto_reload, 1);
	}

	#[test]
	fn unknown_modes_are_skipped_for_the_default()
	{
		let (weapon, _) = parse("script = \"s\"\nfire = {\n modes = [\"Nope\", \"Burst\"]\n}\n");
		let weapon = weapon.unwrap();

		assert_eq!(weapon.def.fire_mode_flags, MODE_BURST);
		assert_eq!(weapon.def.default_mode, MODE_BURST);

		let (weapon, _) = parse("script = \"s\"\nfire = {\n modes = [\"Nope\"]\n}\n");

		assert_eq!(weapon.unwrap().def.fire_mode_flags, MODE_SEMI);
	}

	#[test]
	fn a_file_without_a_script_is_refused()
	{
		let (weapon, _) = parse("name = \"x\"\n");

		assert_eq!(weapon.unwrap_err(), WeaponError::NoScript);
	}
}
