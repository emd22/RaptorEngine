#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Level {
	Debug,
	Info,
	Warning,
	Error,
}

pub trait Log: Send + Sync {
	fn log(&self, level: Level, message: &str);
}
