#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum LogLevel
{
	Print = 0,
	Info = 1,
	Warning = 2,
	Error = 3,
}

pub const CATEGORY_CORE: i32 = 0;
pub const CATEGORY_SCRIPT: i32 = 6;

pub trait Host
{
	fn read_include(&mut self, path: &[u8], extension: &[u8]) -> Option<Vec<u8>>;
	fn log(&mut self, level: LogLevel, category: i32, message: &[u8]);
}
