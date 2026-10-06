use std::path::Path;

use raptor_config::host::{Host, LogLevel};
use raptor_config::model::{Entry, Kind, Primitive};

#[derive(Clone, Debug, PartialEq)]
pub struct HeadBob {
	pub enabled: bool,
	pub scale_x: f32,
	pub scale_y: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub struct GameConfig {
	pub window_title: String,
	pub window_width: u32,
	pub window_height: u32,
	pub scene: Option<String>,
	pub blockout: Option<String>,
	pub head_bob: Option<HeadBob>,
}

impl Default for GameConfig {
	fn default() -> Self {
		Self {
			window_title: "Raptor Engine".to_owned(),
			window_width: 800,
			window_height: 800,
			scene: None,
			blockout: None,
			head_bob: None,
		}
	}
}

struct FsHost;

impl Host for FsHost {
	fn read_include(&mut self, path: &[u8], extension: &[u8]) -> Option<Vec<u8>> {
		let mut name = String::from_utf8_lossy(path).into_owned();

		let extension = String::from_utf8_lossy(extension);

		if Path::new(&name).extension().is_none() {
			name.push_str(&extension);
		}

		std::fs::read(name).ok()
	}

	fn log(&mut self, level: LogLevel, _category: i32, message: &[u8]) {
		let message = String::from_utf8_lossy(message);

		match level {
			LogLevel::Error => raptor_core::log_error!("{message}"),
			LogLevel::Warning => raptor_core::log_warn!("{message}"),
			_ => raptor_core::log_info!("{message}"),
		}
	}
}

fn find<'a>(entries: &'a [Entry], name: &str) -> Option<&'a Entry> {
	entries.iter().find(|entry| entry.name == name.as_bytes())
}

fn number(primitive: &Primitive) -> Option<f64> {
	match primitive.kind {
		Kind::Int => Some(primitive.int_value as f64),
		Kind::Float => Some(f64::from(primitive.float_value)),
		_ => None,
	}
}

fn text(primitive: &Primitive) -> Option<String> {
	primitive
		.string_value
		.as_ref()
		.map(|bytes| String::from_utf8_lossy(bytes).into_owned())
}

fn member_number(entry: &Entry, name: &str) -> Option<f64> {
	find(&entry.members, name).and_then(|member| number(&member.value))
}

impl GameConfig {
	pub fn parse(data: &[u8]) -> Self {
		let parsed = raptor_config::parse(data, None, b".conf", &mut FsHost);

		let mut config = Self::default();

		if let Some(window) = find(&parsed.entries, "Window") {
			if let Some(width) = member_number(window, "Width") {
				config.window_width = width as u32;
			}

			if let Some(height) = member_number(window, "Height") {
				config.window_height = height as u32;
			}

			if let Some(title) =
				find(&window.members, "Title").and_then(|member| text(&member.value))
			{
				config.window_title = title;
			}
		}

		if let Some(bob) = find(&parsed.entries, "HeadBob") {
			config.head_bob = Some(HeadBob {
				enabled: member_number(bob, "Enabled").unwrap_or(1.0) != 0.0,
				scale_x: member_number(bob, "ScaleX").unwrap_or(0.011) as f32,
				scale_y: member_number(bob, "ScaleY").unwrap_or(0.018) as f32,
			});
		}

		config.scene = find(&parsed.entries, "Scene").and_then(|entry| text(&entry.value));
		config.blockout = find(&parsed.entries, "blockout").and_then(|entry| text(&entry.value));

		config
	}

	pub fn load(path: impl AsRef<Path>) -> Self {
		match std::fs::read(path.as_ref()) {
			Ok(data) => Self::parse(&data),
			Err(error) => {
				raptor_core::log_error!("Could not read '{}': {error}", path.as_ref().display());
				Self::default()
			}
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_main_conf_layout_parses() {
		let config = GameConfig::parse(
			b"Window = {\n\tWidth = 900\n\tHeight = 640\n\tTitle = \"Raptor Engine\"\n}\nScene = \"Demo\"\nblockout = \"a/b.prx\"\nHeadBob = {\n\tEnabled = 1\n\tScaleX = 0.046\n\tScaleY = 0.047\n}\n",
		);

		assert_eq!(config.window_width, 900);
		assert_eq!(config.window_height, 640);
		assert_eq!(config.window_title, "Raptor Engine");
		assert_eq!(config.scene.as_deref(), Some("Demo"));
		assert_eq!(config.blockout.as_deref(), Some("a/b.prx"));

		let bob = config.head_bob.unwrap();

		assert!(bob.enabled);
		assert!((bob.scale_x - 0.046).abs() < 1e-6);
	}

	#[test]
	fn missing_entries_fall_back() {
		let config = GameConfig::parse(b"");

		assert_eq!(config, GameConfig::default());
	}
}
