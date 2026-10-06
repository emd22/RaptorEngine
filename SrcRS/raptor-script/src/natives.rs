#![allow(improper_ctypes_definitions)]

use std::collections::HashMap;
use std::ffi::{CStr, CString, c_char, c_int, c_void};
use std::sync::{Mutex, OnceLock};

#[cfg(target_arch = "aarch64")]
use std::arch::aarch64::{float32x4_t, vabsq_f32, vrndmq_f32, vrndnq_f32};
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::{
	__m128, _MM_FROUND_TO_NEAREST_INT, _mm_andnot_ps, _mm_floor_ps, _mm_round_ps, _mm_set1_ps,
};

#[cfg(target_arch = "aarch64")]
type Float4 = float32x4_t;
#[cfg(target_arch = "x86_64")]
type Float4 = __m128;

unsafe extern "C" {
	fn printf(format: *const c_char, ...) -> c_int;
}

#[derive(Clone, Copy)]
struct Native(usize);

fn registry() -> &'static Mutex<HashMap<CString, Native>> {
	static REGISTRY: OnceLock<Mutex<HashMap<CString, Native>>> = OnceLock::new();

	REGISTRY.get_or_init(|| {
		let mut natives = HashMap::new();
		let mut add = |name: &str, function: usize| {
			natives.insert(CString::new(name).unwrap_or_default(), Native(function));
		};

		add("printf", printf as usize);
		add("float4_round", float4_round as usize);
		add("float4_floor", float4_floor as usize);
		add("float3_abs", float3_abs as usize);
		add("float_sign", float_sign as usize);

		Mutex::new(natives)
	})
}

#[cfg(target_arch = "aarch64")]
extern "C" fn float4_round(value: Float4) -> Float4 {
	// SAFETY: NEON is always present on aarch64.
	unsafe { vrndnq_f32(value) }
}

#[cfg(target_arch = "aarch64")]
extern "C" fn float4_floor(value: Float4) -> Float4 {
	// SAFETY: NEON is always present on aarch64.
	unsafe { vrndmq_f32(value) }
}

#[cfg(target_arch = "aarch64")]
extern "C" fn float3_abs(value: Float4) -> Float4 {
	// SAFETY: NEON is always present on aarch64.
	unsafe { vabsq_f32(value) }
}

#[cfg(target_arch = "x86_64")]
extern "C" fn float4_round(value: Float4) -> Float4 {
	// SAFETY: the engine requires SSE4.1.
	unsafe { _mm_round_ps::<_MM_FROUND_TO_NEAREST_INT>(value) }
}

#[cfg(target_arch = "x86_64")]
extern "C" fn float4_floor(value: Float4) -> Float4 {
	// SAFETY: the engine requires SSE4.1.
	unsafe { _mm_floor_ps(value) }
}

#[cfg(target_arch = "x86_64")]
extern "C" fn float3_abs(value: Float4) -> Float4 {
	// SAFETY: SSE is always present on x86_64.
	unsafe { _mm_andnot_ps(_mm_set1_ps(-0.0), value) }
}

extern "C" fn float_sign(value: f32) -> f32 {
	1.0f32.copysign(value)
}

pub fn register(name: &str, function: *const c_void) {
	let Ok(name) = CString::new(name) else {
		return;
	};

	if let Ok(mut registry) = registry().lock() {
		registry.insert(name, Native(function as usize));
	}
}

pub fn find(name: &CStr) -> Option<*const c_void> {
	registry()
		.lock()
		.ok()
		.and_then(|registry| registry.get(name).copied())
		.map(|native| native.0 as *const c_void)
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_pure_natives_are_registered() {
		for name in [
			"printf",
			"float4_round",
			"float4_floor",
			"float3_abs",
			"float_sign",
		] {
			assert!(find(&CString::new(name).unwrap()).is_some());
		}

		assert!(find(&CString::new("no_such_function").unwrap()).is_none());
	}

	#[test]
	fn a_registered_host_function_is_found_by_name() {
		extern "C" fn host() {}

		register("test_host_function", host as *const c_void);

		assert_eq!(
			find(&CString::new("test_host_function").unwrap()),
			Some(host as *const c_void)
		);
	}

	#[test]
	fn the_sign_of_zero_and_negatives() {
		assert_eq!(float_sign(0.0), 1.0);
		assert_eq!(float_sign(-3.0), -1.0);
		assert_eq!(float_sign(-0.0), -1.0);
	}
}
