#![cfg_attr(feature = "simde", allow(unused_unsafe))]

#[macro_use]
mod macros;

pub mod vec3d;
pub mod vec3f;
pub mod vec4f;
pub mod vec4u;

pub mod mat4;
pub mod mat4f;
pub mod bounds;
pub mod frustum;
pub mod util;

pub use vec3d::Vec3d;
pub use vec3f::Vec3f;
pub use vec4f::Vec4f;
pub use vec4u::Vec4u;

pub use bounds::{Aabb, Obb, Ray};
pub use frustum::Frustum;
pub use mat4f::Mat4f;

pub use util::*;

// Neon implementation
#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
mod vec3f_neon;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
pub use vec3f_neon::vec3f_platform;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
mod vec4f_neon;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
pub use vec4f_neon::vec4f_platform;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
mod vec4u_neon;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
pub use vec4u_neon::vec4u_platform;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
mod mat4_neon;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
pub use mat4_neon::mat4f_platform;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
mod vec3d_neon;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
mod vec2d_neon;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
pub use vec3d_neon::vec3d_platform;

#[cfg(all(target_arch = "aarch64", not(feature = "simde")))]
pub use vec2d_neon::vec2d_platform;

// SSE/AVX implementation. With the `simde` feature the x86 intrinsics are emulated on NEON, so this
// code can be tested on an aarch64 machine.
#[cfg(target_arch = "x86_64")]
mod x86
{
	pub use core::arch::x86_64::*;
}

#[cfg(all(feature = "simde", not(target_arch = "x86_64")))]
#[path = "x86_shim.rs"]
mod x86;

#[cfg(all(
	target_arch = "x86_64",
	not(all(target_feature = "sse4.1", target_feature = "avx", target_feature = "fma"))
))]
compile_error!(
	"x86_64 builds need SSE4.1, AVX and FMA. Build with `-C target-feature=+sse4.1,+avx,+fma` \
	 (SrcRS/.cargo/config.toml sets this)"
);

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
mod vec3f_avx;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub use vec3f_avx::vec3f_platform;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
mod vec4f_avx;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub use vec4f_avx::vec4f_platform;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
mod vec4u_avx;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub use vec4u_avx::vec4u_platform;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
mod mat4_avx;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub use mat4_avx::mat4f_platform;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
mod vec3d_avx;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub use vec3d_avx::vec3d_platform;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
mod vec2d_avx;

#[cfg(any(target_arch = "x86_64", feature = "simde"))]
pub use vec2d_avx::vec2d_platform;

mod quat;
pub use quat::quat_platform;

#[cfg(test)]
mod mat4_tests;

#[cfg(test)]
mod geometry_tests;

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
