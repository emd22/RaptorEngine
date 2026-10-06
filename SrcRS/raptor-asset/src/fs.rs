use std::path::{Path, PathBuf};

use raptor_config::host::{Host, LogLevel};
use raptor_core::log::{self, Category, Severity};

pub fn executable_dir() -> Option<PathBuf> {
	std::env::current_exe()
		.ok()
		.and_then(|path| path.parent().map(Path::to_path_buf))
}

/// Where a path relative to the game's files is: beside the executable if it exists there, and in
/// the base directory otherwise
pub fn resolve_path(relative: impl AsRef<Path>) -> PathBuf {
	let relative = relative.as_ref();

	if let Some(directory) = executable_dir() {
		let candidate = directory.join(relative);

		if candidate.exists() {
			return candidate;
		}
	}

	raptor_core::paths::base_dir().join(relative)
}

pub fn read_resolved(relative: impl AsRef<Path>) -> std::io::Result<Vec<u8>> {
	std::fs::read(resolve_path(relative))
}

pub fn config_constants_path() -> PathBuf {
	resolve_path("Config/Internal/Constants.conf")
}

/// What config parsing asks of the engine: files for includes, and somewhere to log
pub struct ConfigHost;

fn category(index: i32) -> Option<Category> {
	u32::try_from(index).ok().and_then(Category::from_index)
}

impl Host for ConfigHost {
	fn read_include(&mut self, path: &[u8], extension: &[u8]) -> Option<Vec<u8>> {
		let path = String::from_utf8_lossy(path).into_owned();
		let mut path = PathBuf::from(path);

		if path.extension().is_none() {
			let mut name = path.into_os_string();
			name.push(String::from_utf8_lossy(extension).as_ref());
			path = PathBuf::from(name);
		}

		std::fs::read(&path).or_else(|_| read_resolved(&path)).ok()
	}

	fn log(&mut self, level: LogLevel, index: i32, message: &[u8]) {
		let severity = match level {
			LogLevel::Print => Severity::None,
			LogLevel::Info => Severity::Info,
			LogLevel::Warning => Severity::Warning,
			LogLevel::Error => Severity::Error,
		};

		log::write(
			severity,
			category(index),
			format_args!("{}", String::from_utf8_lossy(message)),
		);
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn a_missing_include_is_none() {
		assert!(
			ConfigHost
				.read_include(b"/no/such/file", b".conf")
				.is_none()
		);
	}

	#[test]
	fn paths_resolve_into_the_base_directory_when_not_beside_the_executable() {
		let path = resolve_path("Config/Internal/Constants.conf");

		assert!(path.ends_with("Config/Internal/Constants.conf"));
	}
}
