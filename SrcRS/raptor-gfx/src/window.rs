use std::ffi::{CStr, CString};
use std::sync::atomic::{AtomicU32, Ordering};

use ash::vk;
use sdl3_sys::everything as sdl;

#[derive(Debug)]
pub struct WindowError(pub String);

impl std::fmt::Display for WindowError {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		f.write_str(&self.0)
	}
}

impl std::error::Error for WindowError {}

fn sdl_error() -> String {
	// SAFETY: SDL_GetError always returns a valid NUL-terminated string.
	unsafe { CStr::from_ptr(sdl::SDL_GetError()) }
		.to_string_lossy()
		.into_owned()
}

pub trait WindowBackend: Send + Sync {
	fn instance_extensions(&self) -> Vec<CString>;

	fn create_surface(&self, instance: vk::Instance) -> Result<vk::SurfaceKHR, WindowError>;

	fn destroy_surface(&self, instance: vk::Instance, surface: vk::SurfaceKHR);

	fn query_size(&self) -> Option<(u32, u32)>;

	fn set_title(&self, title: &str);

	fn set_relative_mouse_mode(&self, enabled: bool);

	fn mouse_position(&self) -> (f32, f32);

	fn warp_mouse(&self, position: (f32, f32));

	fn is_focused(&self) -> bool;
}

pub struct Window {
	backend: Box<dyn WindowBackend>,
	width: AtomicU32,
	height: AtomicU32,
}

impl Window {
	pub const MAX_DIMENSION: u32 = 8000;

	pub fn new(title: &str, size: (u32, u32)) -> Result<Self, WindowError> {
		Ok(Self::from_backend(
			Box::new(SdlWindow::create(title, size)?),
			size,
		))
	}

	pub fn from_backend(backend: Box<dyn WindowBackend>, size: (u32, u32)) -> Self {
		Self {
			backend,
			width: AtomicU32::new(size.0),
			height: AtomicU32::new(size.1),
		}
	}

	pub fn size(&self) -> (u32, u32) {
		(
			self.width.load(Ordering::Relaxed),
			self.height.load(Ordering::Relaxed),
		)
	}

	pub fn aspect_ratio(&self) -> f32 {
		let (width, height) = self.size();

		width as f32 / height as f32
	}

	pub fn handle_resize(&self) {
		if let Some((width, height)) = self.backend.query_size() {
			self.width.store(width, Ordering::Relaxed);
			self.height.store(height, Ordering::Relaxed);
		}
	}

	pub fn set_title(&self, title: &str) {
		self.backend.set_title(title);
	}

	pub fn instance_extensions(&self) -> Vec<CString> {
		self.backend.instance_extensions()
	}

	pub fn create_surface(&self, instance: vk::Instance) -> Result<vk::SurfaceKHR, WindowError> {
		self.backend.create_surface(instance)
	}

	pub fn destroy_surface(&self, instance: vk::Instance, surface: vk::SurfaceKHR) {
		self.backend.destroy_surface(instance, surface);
	}

	pub fn set_relative_mouse_mode(&self, enabled: bool) {
		self.backend.set_relative_mouse_mode(enabled);
	}

	pub fn mouse_position(&self) -> (f32, f32) {
		self.backend.mouse_position()
	}

	pub fn warp_mouse(&self, position: (f32, f32)) {
		self.backend.warp_mouse(position);
	}

	pub fn is_focused(&self) -> bool {
		self.backend.is_focused()
	}
}

pub struct SdlWindow {
	window: *mut sdl::SDL_Window,
}

// SAFETY: SDL windows are only touched from the thread that owns the video subsystem in
// practice, and the handle itself is just an opaque pointer.
unsafe impl Send for SdlWindow {}
// SAFETY: as above.
unsafe impl Sync for SdlWindow {}

impl SdlWindow {
	pub fn create(title: &str, size: (u32, u32)) -> Result<Self, WindowError> {
		if size.0 > Window::MAX_DIMENSION || size.1 > Window::MAX_DIMENSION {
			return Err(WindowError(format!(
				"Window size is too large! (Size: {}x{})",
				size.0, size.1
			)));
		}

		let title = CString::new(title).map_err(|error| WindowError(error.to_string()))?;

		point_sdl_at_the_vulkan_sdk();

		// SAFETY: plain SDL calls with valid arguments.
		let window = unsafe {
			if !sdl::SDL_InitSubSystem(sdl::SDL_INIT_VIDEO) {
				return Err(WindowError(format!(
					"Could not initialise SDL video (SDL err: {})",
					sdl_error()
				)));
			}

			sdl::SDL_SetHint(sdl::SDL_HINT_WINDOWS_RAW_KEYBOARD, c"1".as_ptr());

			sdl::SDL_CreateWindow(
				title.as_ptr(),
				size.0 as i32,
				size.1 as i32,
				sdl::SDL_WINDOW_VULKAN | sdl::SDL_WINDOW_RESIZABLE,
			)
		};

		if window.is_null() {
			return Err(WindowError(format!(
				"Could not create SDL window (SDL err: {})",
				sdl_error()
			)));
		}

		Ok(Self { window })
	}

	pub fn raw(&self) -> *mut sdl::SDL_Window {
		self.window
	}
}

impl WindowBackend for SdlWindow {
	fn instance_extensions(&self) -> Vec<CString> {
		let mut count = 0;

		// SAFETY: SDL returns an array of `count` static strings, or null.
		let names = unsafe { sdl::SDL_Vulkan_GetInstanceExtensions(&mut count) };

		if names.is_null() {
			return Vec::new();
		}

		// SAFETY: `names` points to `count` valid NUL-terminated strings.
		unsafe { std::slice::from_raw_parts(names, count as usize) }
			.iter()
			// SAFETY: each entry is a valid NUL-terminated string.
			.map(|&name| unsafe { CStr::from_ptr(name) }.to_owned())
			.collect()
	}

	fn create_surface(&self, instance: vk::Instance) -> Result<vk::SurfaceKHR, WindowError> {
		let mut surface = vk::SurfaceKHR::null();

		// SAFETY: the window and instance are live and `surface` is writable.
		let created = unsafe {
			sdl::SDL_Vulkan_CreateSurface(self.window, instance, std::ptr::null(), &mut surface)
		};

		if created {
			Ok(surface)
		} else {
			Err(WindowError(format!(
				"Could not attach Vulkan instance to window! (SDL err: {})",
				sdl_error()
			)))
		}
	}

	fn destroy_surface(&self, instance: vk::Instance, surface: vk::SurfaceKHR) {
		// SAFETY: the surface belongs to the instance and nothing presents to it any more.
		unsafe { sdl::SDL_Vulkan_DestroySurface(instance, surface, std::ptr::null()) };
	}

	fn query_size(&self) -> Option<(u32, u32)> {
		let (mut width, mut height) = (0, 0);

		// SAFETY: the window is live and both outputs are writable.
		if unsafe { sdl::SDL_GetWindowSize(self.window, &mut width, &mut height) } {
			Some((width as u32, height as u32))
		} else {
			raptor_core::log_error!(Render; "Error retrieving window size from SDL! (SDL err: {})", sdl_error());
			None
		}
	}

	fn set_title(&self, title: &str) {
		if let Ok(title) = CString::new(title) {
			// SAFETY: the window is live and the title is NUL-terminated.
			unsafe { sdl::SDL_SetWindowTitle(self.window, title.as_ptr()) };
		}
	}

	fn set_relative_mouse_mode(&self, enabled: bool) {
		// SAFETY: the window is live.
		unsafe { sdl::SDL_SetWindowRelativeMouseMode(self.window, enabled) };
	}

	fn mouse_position(&self) -> (f32, f32) {
		let (mut x, mut y) = (0.0, 0.0);

		// SAFETY: both outputs are writable.
		unsafe { sdl::SDL_GetMouseState(&mut x, &mut y) };

		(x, y)
	}

	fn warp_mouse(&self, position: (f32, f32)) {
		// SAFETY: the window is live.
		unsafe { sdl::SDL_WarpMouseInWindow(self.window, position.0, position.1) };
	}

	fn is_focused(&self) -> bool {
		// SAFETY: the window is live.
		unsafe { sdl::SDL_GetWindowFlags(self.window) & sdl::SDL_WINDOW_INPUT_FOCUS != 0 }
	}
}

impl Drop for SdlWindow {
	fn drop(&mut self) {
		// SAFETY: the window is live and owned by this value.
		unsafe {
			sdl::SDL_DestroyWindow(self.window);
			sdl::SDL_QuitSubSystem(sdl::SDL_INIT_VIDEO);
		}
	}
}

fn sdk_vulkan_library() -> Option<std::path::PathBuf> {
	if let Some(sdk) = std::env::var_os("VULKAN_SDK").filter(|sdk| !sdk.is_empty()) {
		let library = std::path::Path::new(&sdk).join("lib/libvulkan.dylib");

		if library.exists() {
			return Some(library);
		}
	}

	let mut sdks: Vec<_> = std::fs::read_dir(std::path::Path::new(&std::env::var_os("HOME")?).join("VulkanSDK"))
		.ok()?
		.filter_map(Result::ok)
		.map(|entry| entry.path().join("macOS/lib/libvulkan.dylib"))
		.filter(|library| library.exists())
		.collect();

	sdks.sort();

	sdks.pop()
}

fn point_sdl_at_the_vulkan_sdk() {
	if cfg!(target_os = "macos") && std::env::var_os("SDL_VULKAN_LIBRARY").is_none() {
		if let Some(library) = sdk_vulkan_library().and_then(|library| CString::new(library.to_string_lossy().as_bytes()).ok()) {
			// SAFETY: plain SDL calls with NUL terminated strings that SDL copies.
			unsafe { sdl::SDL_SetHint(sdl::SDL_HINT_VULKAN_LIBRARY, library.as_ptr()) };
		}
	}
}

pub fn vulkan_loader() -> Result<vk::PFN_vkGetInstanceProcAddr, WindowError> {
	// SAFETY: loading the default Vulkan library is allowed once the video subsystem is up.
	unsafe {
		if !sdl::SDL_Vulkan_LoadLibrary(std::ptr::null()) {
			return Err(WindowError(format!(
				"Could not load the Vulkan library (SDL err: {})",
				sdl_error()
			)));
		}

		let function = sdl::SDL_Vulkan_GetVkGetInstanceProcAddr();

		function
			.map(|function| std::mem::transmute(function))
			.ok_or_else(|| WindowError("SDL has no vkGetInstanceProcAddr".to_owned()))
	}
}
