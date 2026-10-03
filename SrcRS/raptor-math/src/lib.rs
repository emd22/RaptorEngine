#[macro_use]
mod macros;

pub mod vec3d;
pub mod vec3f;

pub mod util;

pub use vec3d::Vec3d;
pub use vec3f::Vec3f;

pub use util::*;

// Neon implementation
#[cfg(target_arch = "aarch64")]
mod vec3f_neon;

#[cfg(target_arch = "aarch64")]
pub use vec3f_neon::vec3f_platform;

#[cfg(target_arch = "aarch64")]
mod vec3d_neon;

#[cfg(target_arch = "aarch64")]
mod vec2d_neon;

#[cfg(target_arch = "aarch64")]
pub use vec3d_neon::vec3d_platform;

#[cfg(target_arch = "aarch64")]
pub use vec2d_neon::vec2d_platform;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogLevel
{
	Print,
	Error,
}

pub trait Log
{
	fn log(&mut self, level: LogLevel, message: &str);
}
