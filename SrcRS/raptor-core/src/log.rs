use std::fmt::{self, Arguments};
use std::fs::File;
use std::io::Write;
use std::path::Path;
use std::sync::Mutex;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Severity
{
	None,
	Debug,
	Info,
	Warning,
	Error,
	Fatal,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Category
{
	Core,
	Shader,
	Render,
	Physics,
	Memory,
	Asset,
	Script,
}

impl Category
{
	pub const fn from_index(index: u32) -> Option<Category>
	{
		Some(match index {
			0 => Category::Core,
			1 => Category::Shader,
			2 => Category::Render,
			3 => Category::Physics,
			4 => Category::Memory,
			5 => Category::Asset,
			6 => Category::Script,
			_ => return None,
		})
	}

	const fn label(self) -> &'static str
	{
		match self {
			Category::Core => "[CORE] ",
			Category::Shader => "[SHADER] ",
			Category::Render => "[RENDER] ",
			Category::Physics => "[PHYSICS] ",
			Category::Memory => "[MEMORY] ",
			Category::Asset => "[ASSET] ",
			Category::Script => "[SCRIPT] ",
		}
	}

	const fn color(self) -> &'static str
	{
		match self {
			Category::Core => "\x1b[38;5;116m",
			Category::Shader => "\x1b[38;5;218m",
			Category::Render => "\x1b[38;5;215m",
			Category::Physics => "\x1b[38;5;154m",
			Category::Memory => "\x1b[38;5;212m",
			Category::Asset => "\x1b[38;5;192m",
			Category::Script => "\x1b[38;5;84m",
		}
	}
}

impl Severity
{
	const fn label(self) -> &'static str
	{
		match self {
			Severity::None => "",
			Severity::Debug => "[DEBUG] ",
			Severity::Info => "[INFO]  ",
			Severity::Warning => "[WARN]  ",
			Severity::Error => "[ERROR] ",
			Severity::Fatal => "[FATAL] ",
		}
	}

	const fn color(self) -> &'static str
	{
		match self {
			Severity::None => "",
			Severity::Debug => "\x1b[92m",
			Severity::Info => "\x1b[94m",
			Severity::Warning => "\x1b[93m",
			Severity::Error => "\x1b[91m",
			Severity::Fatal => "\x1b[1;91m",
		}
	}
}

const RESET: &str = "\x1b[0m";

struct Sinks
{
	file: Option<File>,
	min_severity: Severity,
	colors: bool,
}

static SINKS: Mutex<Sinks> = Mutex::new(Sinks {
	file: None,
	min_severity: if cfg!(debug_assertions) {
		Severity::Debug
	} else {
		Severity::Info
	},
	colors: true,
});

fn sinks() -> std::sync::MutexGuard<'static, Sinks>
{
	SINKS
		.lock()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
}

pub fn create_file(path: impl AsRef<Path>) -> std::io::Result<()>
{
	let file = File::create(path)?;

	sinks().file = Some(file);

	Ok(())
}

pub fn close_file()
{
	sinks().file = None;
}

pub fn set_min_severity(severity: Severity)
{
	sinks().min_severity = severity;
}

pub fn set_colors(enabled: bool)
{
	sinks().colors = enabled;
}

pub fn enabled(severity: Severity) -> bool
{
	severity >= sinks().min_severity || severity == Severity::None
}

pub fn write(severity: Severity, category: Option<Category>, args: Arguments)
{
	let mut sinks = sinks();

	if severity != Severity::None && severity < sinks.min_severity {
		return;
	}

	let mut plain = String::new();
	let mut colored = String::new();

	if let Some(category) = category {
		plain.push_str(category.label());
		colored.push_str(category.color());
		colored.push_str(category.label());
	}

	plain.push_str(severity.label());

	if severity != Severity::None {
		colored.push_str(severity.color());
		colored.push_str(severity.label());
		colored.push_str(RESET);
	}

	let mut message = String::new();
	let _ = fmt::write(&mut message, args);

	plain.push_str(&message);
	colored.push_str(&message);
	plain.push('\n');
	colored.push('\n');

	let text = if sinks.colors { colored } else { plain.clone() };

	let _ = std::io::stdout().lock().write_all(text.as_bytes());

	if let Some(file) = sinks.file.as_mut() {
		let _ = file.write_all(plain.as_bytes());
	}
}

#[macro_export]
macro_rules! log_at {
	($severity:expr, $category:ident; $($arg:tt)+) => {
		$crate::log::write($severity, Some($crate::log::Category::$category), format_args!($($arg)+))
	};
	($severity:expr, $($arg:tt)+) => {
		$crate::log::write($severity, None, format_args!($($arg)+))
	};
}

#[macro_export]
macro_rules! log_debug {
	($($arg:tt)+) => { $crate::log_at!($crate::log::Severity::Debug, $($arg)+) };
}

#[macro_export]
macro_rules! log_info {
	($($arg:tt)+) => { $crate::log_at!($crate::log::Severity::Info, $($arg)+) };
}

#[macro_export]
macro_rules! log_warn {
	($($arg:tt)+) => { $crate::log_at!($crate::log::Severity::Warning, $($arg)+) };
}

#[macro_export]
macro_rules! log_error {
	($($arg:tt)+) => { $crate::log_at!($crate::log::Severity::Error, $($arg)+) };
}

#[macro_export]
macro_rules! log_fatal {
	($($arg:tt)+) => { $crate::log_at!($crate::log::Severity::Fatal, $($arg)+) };
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn categories_and_severities_format()
	{
		log_info!(Core; "hello {}", 3);
		log_error!("plain {}", "x");
		assert_eq!(Category::from_index(6), Some(Category::Script));
		assert_eq!(Category::from_index(7), None);
		assert!(Severity::Error > Severity::Info);
	}

	#[test]
	fn file_sink_receives_plain_text()
	{
		let path = std::env::temp_dir().join("raptor_core_log_test.txt");

		create_file(&path).unwrap();
		write(
			Severity::Warning,
			Some(Category::Asset),
			format_args!("careful"),
		);
		close_file();

		let text = std::fs::read_to_string(&path).unwrap();
		assert_eq!(text, "[ASSET] [WARN]  careful\n");
		let _ = std::fs::remove_file(path);
	}
}
