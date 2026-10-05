pub mod compiler;
pub mod datapack;
pub mod preproc;
pub mod program;

pub use preproc::{Macro, Output, ReflectionEntry, ReflectionType, Stage, process};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogLevel
{
	Print,
	Warning,
	Error,
}

pub trait Log
{
	fn log(&mut self, level: LogLevel, message: &str);
}
