use std::ffi::{CStr, CString, c_char, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;

use ash::vk::{self, Handle};
use raptor_gpu::{Device, Instance, InstanceConfig, Level, Log};

use crate::RxLogSink;

const CATEGORY_RENDER: i32 = 2;

struct CLog
{
	user: *mut c_void,
	log: Option<crate::LogFn>,
}

// SAFETY: the host promises its log callback and user pointer can be used from any thread.
unsafe impl Send for CLog {}
// SAFETY: as above.
unsafe impl Sync for CLog {}

impl Log for CLog
{
	fn log(&self, level: Level, message: &str)
	{
		let Some(log) = self.log else { return };

		let level = match level {
			Level::Debug => 4,
			Level::Info => 1,
			Level::Warning => 2,
			Level::Error => 3,
		};

		// SAFETY: the host promised `log` accepts a pointer and length pair.
		unsafe {
			log(
				self.user,
				level,
				CATEGORY_RENDER,
				message.as_ptr().cast(),
				message.len(),
			)
		};
	}
}

pub struct RxGpuInstance(pub(crate) Instance);
pub struct RxGpuDevice
{
	pub(crate) device: Device,
	info: RxGpuDeviceInfo,
}

#[repr(C)]
pub struct RxGpuInstanceConfig
{
	pub app_name: *const c_char,
	pub api_version: u32,
	pub extensions: *const *const c_char,
	pub extension_count: usize,
	pub layers: *const *const c_char,
	pub layer_count: usize,
	pub enumerate_portability: u8,
	pub debug_messenger: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxGpuDeviceInfo
{
	pub physical: *mut c_void,
	pub device: *mut c_void,
	pub graphics_queue: *mut c_void,
	pub present_queue: *mut c_void,
	pub transfer_queue: *mut c_void,
	pub graphics_family: u32,
	pub present_family: u32,
	pub transfer_family: u32,
	pub max_sampler_anisotropy: f32,
	pub supports_cube_arrays: u8,
}

fn log_of(sink: &RxLogSink) -> Arc<dyn Log>
{
	Arc::new(CLog {
		user: sink.user,
		log: sink.log,
	})
}

/// # Safety
///
/// `names` must point to `count` NUL-terminated strings, or be null when `count` is 0.
unsafe fn cstrings(names: *const *const c_char, count: usize) -> Vec<CString>
{
	if names.is_null() {
		return Vec::new();
	}

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts(names, count) }
		.iter()
		// SAFETY: guaranteed by the caller.
		.map(|&name| unsafe { CStr::from_ptr(name) }.to_owned())
		.collect()
}

/// # Safety
///
/// `get_instance_proc_addr` must be the process's `vkGetInstanceProcAddr`. `config` must point to a
/// valid `RxGpuInstanceConfig` whose strings are NUL-terminated, and `log` to a valid `RxLogSink`
/// whose `user` outlives the instance.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_instance_create(
	get_instance_proc_addr: vk::PFN_vkGetInstanceProcAddr,
	config: *const RxGpuInstanceConfig,
	log: *const RxLogSink,
) -> *mut RxGpuInstance
{
	// SAFETY: guaranteed by the caller.
	let (config, log) = unsafe { (&*config, log_of(&*log)) };

	// SAFETY: guaranteed by the caller.
	let (app_name, extensions, layers) = unsafe {
		(
			CStr::from_ptr(config.app_name).to_owned(),
			cstrings(config.extensions, config.extension_count),
			cstrings(config.layers, config.layer_count),
		)
	};

	let config = InstanceConfig {
		app_name,
		api_version: config.api_version,
		extensions,
		layers,
		enumerate_portability: config.enumerate_portability != 0,
		debug_messenger: config.debug_messenger != 0,
	};

	let error_log = log.clone();

	// SAFETY: guaranteed by the caller.
	let result = catch_unwind(AssertUnwindSafe(|| unsafe {
		Instance::create(get_instance_proc_addr, &config, log)
	}));

	match result {
		Ok(Ok(instance)) => Box::into_raw(Box::new(RxGpuInstance(instance))),
		Ok(Err(error)) => {
			error_log.log(Level::Error, &error.to_string());
			std::ptr::null_mut()
		}
		Err(_) => std::ptr::null_mut(),
	}
}

/// # Safety
///
/// `instance` must come from `rx_gpu_instance_create`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_instance_handle(instance: *const RxGpuInstance) -> *mut c_void
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*instance }.0.handle().as_raw() as *mut c_void
}

/// # Safety
///
/// `instance` must be null or come from `rx_gpu_instance_create`, with every object made from it
/// destroyed, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_instance_free(instance: *mut RxGpuInstance)
{
	if !instance.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(instance) });
	}
}

/// # Safety
///
/// `instance` must come from `rx_gpu_instance_create` and outlive the device. `surface` must be a
/// valid surface of that instance.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_device_create(
	instance: *const RxGpuInstance,
	surface: u64,
) -> *mut RxGpuDevice
{
	// SAFETY: guaranteed by the caller.
	let instance = unsafe { &*instance };
	let surface = vk::SurfaceKHR::from_raw(surface);

	let result = catch_unwind(AssertUnwindSafe(|| Device::create(&instance.0, surface)));

	let device = match result {
		Ok(Ok(device)) => device,
		Ok(Err(error)) => {
			instance.0.log().log(Level::Error, &error.to_string());
			return std::ptr::null_mut();
		}
		Err(_) => return std::ptr::null_mut(),
	};

	let families = device.families();
	let caps = device.caps();

	let info = RxGpuDeviceInfo {
		physical: device.physical().as_raw() as *mut c_void,
		device: device.handle().as_raw() as *mut c_void,
		graphics_queue: device.graphics_queue().as_raw() as *mut c_void,
		present_queue: device.present_queue().as_raw() as *mut c_void,
		transfer_queue: device.transfer_queue().as_raw() as *mut c_void,
		graphics_family: families.graphics,
		present_family: families.present,
		transfer_family: families.transfer,
		max_sampler_anisotropy: caps.max_sampler_anisotropy,
		supports_cube_arrays: u8::from(caps.supports_cube_arrays),
	};

	Box::into_raw(Box::new(RxGpuDevice { device, info }))
}

/// # Safety
///
/// `device` must come from `rx_gpu_device_create`. The returned pointer is valid until the device
/// is freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_device_info(device: *const RxGpuDevice) -> *const RxGpuDeviceInfo
{
	// SAFETY: guaranteed by the caller.
	&unsafe { &*device }.info
}

/// # Safety
///
/// `device` must come from `rx_gpu_device_create`, and `format` and `color_space` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_device_surface_format(
	device: *const RxGpuDevice,
	format: *mut i32,
	color_space: *mut i32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let device = unsafe { &*device };

	match catch_unwind(AssertUnwindSafe(|| device.device.surface_format())) {
		Ok(Ok(surface_format)) => {
			// SAFETY: guaranteed by the caller.
			unsafe {
				*format = surface_format.format.as_raw();
				*color_space = surface_format.color_space.as_raw();
			}
			1
		}
		_ => 0,
	}
}

/// # Safety
///
/// `device` must come from `rx_gpu_device_create`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_device_wait_idle(device: *const RxGpuDevice)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*device }.device.wait_idle();
}

/// # Safety
///
/// `device` must be null or come from `rx_gpu_device_create`, with every object made from it
/// destroyed, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gpu_device_free(device: *mut RxGpuDevice)
{
	if !device.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(device) });
	}
}
