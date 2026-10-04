use std::fmt;

use ash::vk;

#[derive(Debug)]
pub struct Error(pub String);

pub type Result<T> = std::result::Result<T, Error>;

impl Error
{
	pub fn new(message: impl Into<String>) -> Self
	{
		Self(message.into())
	}

	pub fn vulkan(message: &str, result: vk::Result) -> Self
	{
		Self(format!("{message}: {result:?}"))
	}
}

impl fmt::Display for Error
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		f.write_str(&self.0)
	}
}

impl std::error::Error for Error {}
