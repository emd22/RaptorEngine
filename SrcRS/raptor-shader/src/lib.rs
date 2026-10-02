pub mod preproc;

pub use preproc::{Macro, Output, ReflectionEntry, ReflectionType, Stage, process};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogLevel {
	Print,
	Error,
}

pub trait Log {
	fn log(&mut self, level: LogLevel, message: &str);
}
