use std::ffi::CString;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, RwLock, RwLockReadGuard};

use ash::vk::{self, Handle};
use ash::{ext, khr};
use raptor_gpu::{
	Allocator, DescriptorCache, Device, DsLayoutCache, FrameLoop, GpuProfiler, Instance,
	InstanceConfig, Level, Log, SamplerCache, SamplerProps,
};

use crate::buffers::{RendererBufferSizes, RendererBuffers};
use crate::commands::CommandBuffer;
use crate::core::GpuCore;
use crate::image::Image;
use crate::limits::FRAMES_IN_FLIGHT;
use crate::log::GfxLog;
use crate::profiler::GpuMarker;
use crate::state::RenderState;
use crate::swapchain::Swapchain;
use crate::texture::Textures;
use crate::upload::UploadContext;
use crate::window::{Window, WindowError, vulkan_loader};

const API_VERSION: u32 = vk::make_api_version(0, 1, 3, 261);

#[derive(Debug)]
pub struct GfxError(pub String);

impl std::fmt::Display for GfxError {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		f.write_str(&self.0)
	}
}

impl std::error::Error for GfxError {}

impl From<raptor_gpu::Error> for GfxError {
	fn from(error: raptor_gpu::Error) -> Self {
		Self(error.to_string())
	}
}

impl From<WindowError> for GfxError {
	fn from(error: WindowError) -> Self {
		Self(error.0)
	}
}

impl From<vk::Result> for GfxError {
	fn from(error: vk::Result) -> Self {
		Self(format!("{error:?}"))
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FrameResult {
	Success,
	GraphicsOutOfDate,
	RenderError,
}

pub struct GfxConfig {
	pub app_name: String,
	pub validation: bool,
	pub buffer_sizes: RendererBufferSizes,
}

pub struct Frame {
	pool: vk::CommandPool,
	cmd: CommandBuffer,
}

impl Frame {
	pub fn cmd(&self) -> &CommandBuffer {
		&self.cmd
	}

	pub fn pool(&self) -> vk::CommandPool {
		self.pool
	}
}

type ResizeHook = Arc<dyn Fn(&Gfx) + Send + Sync>;

pub struct Gfx {
	frames: Vec<Frame>,
	profiler: Mutex<Option<Box<GpuProfiler>>>,
	frame_loop: Option<Box<FrameLoop>>,
	buffers: Option<RendererBuffers>,
	dfg_lut: std::sync::OnceLock<Image>,
	noise_texture: std::sync::OnceLock<Image>,
	textures: Textures,
	upload: Option<UploadContext>,
	swapchain: RwLock<Swapchain>,
	descriptors: Mutex<DescriptorCache>,
	ds_layouts: DsLayoutCache,
	samplers: SamplerCache,
	state: RenderState,
	resize_hook: Mutex<Option<ResizeHook>>,
	did_resize: AtomicBool,
	cube_arrays: bool,
	surface: vk::SurfaceKHR,
	window: Arc<Window>,
	core: Arc<GpuCore>,
}

// SAFETY: the backend's Vulkan state is externally synchronised by its users, as in the engine it
// replaces, and its caches sit behind mutexes.
unsafe impl Send for Gfx {}
// SAFETY: as above.
unsafe impl Sync for Gfx {}

static GLOBAL: RwLock<Option<Arc<Gfx>>> = RwLock::new(None);

pub fn gfx() -> Arc<Gfx> {
	try_gfx().expect("the graphics backend is not initialised")
}

pub fn try_gfx() -> Option<Arc<Gfx>> {
	GLOBAL
		.read()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
		.clone()
}

pub fn install(gfx: Gfx) -> Arc<Gfx> {
	let gfx = Arc::new(gfx);

	*GLOBAL
		.write()
		.unwrap_or_else(|poisoned| poisoned.into_inner()) = Some(gfx.clone());

	gfx
}

pub fn shutdown() -> bool {
	GLOBAL
		.write()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
		.take()
		.is_some()
}

fn instance_extensions(
	loader: vk::PFN_vkGetInstanceProcAddr,
	window: &Window,
) -> Result<(Vec<CString>, bool), GfxError> {
	// SAFETY: `loader` is the process's `vkGetInstanceProcAddr`.
	let entry = unsafe {
		ash::Entry::from_static_fn(ash::StaticFn {
			get_instance_proc_addr: loader,
		})
	};

	// SAFETY: the entry points are loaded.
	let available = unsafe { entry.enumerate_instance_extension_properties(None) }
		.map_err(|result| GfxError(format!("Could not query instance extensions: {result:?}")))?;

	let has = |wanted: &std::ffi::CStr| {
		available
			.iter()
			.any(|extension| extension.extension_name_as_c_str() == Ok(wanted))
	};

	let mut extensions = window.instance_extensions();

	for wanted in [ext::debug_utils::NAME, ext::debug_report::NAME] {
		if has(wanted) {
			extensions.push(wanted.to_owned());
		} else {
			raptor_core::log_warn!(Render; "Instance extension {wanted:?} is not available");
		}
	}

	let portability = has(khr::portability_enumeration::NAME);

	if portability {
		extensions.push(khr::portability_enumeration::NAME.to_owned());
	}

	for name in &extensions {
		raptor_core::log_debug!(Render; "Requesting instance extension {name:?}");
	}

	Ok((extensions, portability))
}

impl Gfx {
	pub fn create(window: Arc<Window>, config: &GfxConfig) -> Result<Self, GfxError> {
		let log: Arc<dyn Log> = Arc::new(GfxLog);
		let loader = vulkan_loader()?;
		let (extensions, portability) = instance_extensions(loader, &window)?;

		let app_name =
			CString::new(config.app_name.as_str()).map_err(|error| GfxError(error.to_string()))?;

		let layers = if config.validation {
			vec![c"VK_LAYER_KHRONOS_validation".to_owned()]
		} else {
			Vec::new()
		};

		// SAFETY: `loader` is the `vkGetInstanceProcAddr` SDL loaded for the process.
		let instance = unsafe {
			Instance::create(
				loader,
				&InstanceConfig {
					app_name,
					api_version: API_VERSION,
					extensions,
					layers,
					enumerate_portability: portability,
					debug_messenger: config.validation,
				},
				log,
			)
		}?;

		let surface = window.create_surface(instance.handle())?;

		let device = Device::create(&instance, surface)?;

		// SAFETY: the instance and device are live and belong together.
		let allocator = unsafe { Allocator::new(&instance, &device) }.map_err(|result| {
			GfxError(format!("Could not create the GPU allocator: {result:?}"))
		})?;

		let caps = device.caps();
		let families = device.families();

		let core = Arc::new(GpuCore::new(instance, device, allocator));

		let size = window.size();
		let swapchain = Swapchain::create(&core, surface, size)?;

		let mut frames = Vec::with_capacity(FRAMES_IN_FLIGHT as usize);

		for _ in 0..FRAMES_IN_FLIGHT {
			let device = core.device();
			let pool = device.create_command_pool(families.graphics)?;
			let cmd = device.allocate_command_buffer(pool)?;

			device.set_object_name(vk::ObjectType::COMMAND_BUFFER, cmd.as_raw(), c"RenderCmd");

			frames.push(Frame {
				pool,
				cmd: CommandBuffer::new(cmd, families.graphics),
			});
		}

		let frame_loop = FrameLoop::create(
			core.device(),
			FRAMES_IN_FLIGHT,
			swapchain.image_count() as u32,
		)?;

		let profiler = GpuProfiler::create(core.device(), families.graphics, FRAMES_IN_FLIGHT);

		let upload = UploadContext::new(&core, families.transfer)?;
		let textures = Textures::new(&core);

		let buffers = RendererBuffers::create(
			&core,
			&textures,
			&upload,
			&config.buffer_sizes,
			caps.supports_cube_arrays,
		)?;

		Ok(Self {
			frames,
			profiler: Mutex::new(profiler),
			frame_loop: Some(frame_loop),
			buffers: Some(buffers),
			dfg_lut: std::sync::OnceLock::new(),
			noise_texture: std::sync::OnceLock::new(),
			textures,
			upload: Some(upload),
			swapchain: RwLock::new(swapchain),
			descriptors: Mutex::new(DescriptorCache::default()),
			ds_layouts: DsLayoutCache::default(),
			samplers: SamplerCache::default(),
			state: RenderState::default(),
			resize_hook: Mutex::new(None),
			did_resize: AtomicBool::new(false),
			cube_arrays: caps.supports_cube_arrays,
			surface,
			window,
			core,
		})
	}

	pub fn core(&self) -> &Arc<GpuCore> {
		&self.core
	}

	pub fn device(&self) -> &Device {
		self.core.device()
	}

	pub fn allocator(&self) -> &Allocator {
		self.core.allocator()
	}

	pub fn window(&self) -> &Arc<Window> {
		&self.window
	}

	pub fn surface(&self) -> vk::SurfaceKHR {
		self.surface
	}

	pub fn state(&self) -> &RenderState {
		&self.state
	}

	pub fn textures(&self) -> &Textures {
		&self.textures
	}

	pub fn buffers(&self) -> &RendererBuffers {
		self.buffers
			.as_ref()
			.expect("the renderer buffers exist until shutdown")
	}

	pub fn upload(&self) -> &UploadContext {
		self.upload
			.as_ref()
			.expect("the upload context exists until shutdown")
	}

	pub fn frame_loop(&self) -> &FrameLoop {
		self.frame_loop
			.as_ref()
			.expect("the frame loop exists until shutdown")
	}

	pub fn supports_cube_arrays(&self) -> bool {
		self.cube_arrays
	}

	pub fn swapchain(&self) -> RwLockReadGuard<'_, Swapchain> {
		self.swapchain
			.read()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn ds_layouts(&self) -> &DsLayoutCache {
		&self.ds_layouts
	}

	pub fn descriptors(&self) -> std::sync::MutexGuard<'_, DescriptorCache> {
		self.descriptors
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn sampler(&self, props: &SamplerProps) -> vk::Sampler {
		// SAFETY: the entry lives as long as the cache, which this backend owns.
		unsafe { &*self.samplers.request(self.device(), props) }.handle
	}

	pub fn set_dfg_lut(&self, image: Image) -> bool {
		self.dfg_lut.set(image).is_ok()
	}

	pub fn dfg_lut(&self) -> Option<&Image> {
		self.dfg_lut.get()
	}

	pub fn set_noise_texture(&self, image: Image) -> bool {
		self.noise_texture.set(image).is_ok()
	}

	pub fn noise_texture(&self) -> Option<&Image> {
		self.noise_texture.get()
	}

	pub fn set_resize_hook(&self, hook: impl Fn(&Gfx) + Send + Sync + 'static) {
		*self
			.resize_hook
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner()) = Some(Arc::new(hook));
	}

	pub fn did_resize(&self) -> bool {
		self.did_resize.load(Ordering::Relaxed)
	}

	pub fn frame_number(&self) -> u32 {
		self.frame_loop()
			.fields
			.frame_number
			.load(Ordering::Relaxed)
	}

	pub fn elapsed_frame_count(&self) -> u32 {
		self.frame_loop().fields.elapsed.load(Ordering::Relaxed)
	}

	pub fn image_index(&self) -> u32 {
		self.frame_loop().fields.image_index.load(Ordering::Relaxed)
	}

	pub fn frame(&self) -> &Frame {
		&self.frames[self.frame_number() as usize]
	}

	pub fn frame_cmd(&self) -> &CommandBuffer {
		self.frame().cmd()
	}

	pub fn begin_frame(&self) -> FrameResult {
		let buffers = self.buffers();

		buffers
			.light
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.rewind();
		buffers
			.bone
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.rewind();

		if let Err(error) = self.frame_loop().begin_frame(self.device()) {
			raptor_core::log_fatal!(Render; "Could not wait for the frame fence: {error:?}");
			panic!("Could not wait for the frame fence: {error:?}");
		}

		self.core.process_deletions();

		self.acquire_swapchain_image()
	}

	fn acquire_swapchain_image(&self) -> FrameResult {
		let swapchain = self.swapchain().handle();
		let result = self.frame_loop().acquire(self.device(), swapchain);

		match result {
			vk::Result::SUCCESS => FrameResult::Success,
			vk::Result::ERROR_OUT_OF_DATE_KHR => {
				self.rebuild_to_resized_window();
				FrameResult::GraphicsOutOfDate
			}
			other => {
				raptor_core::log_error!(Render; "Error getting next swapchain image! Status: {:x}", other.as_raw());
				FrameResult::RenderError
			}
		}
	}

	pub fn rebuild_to_resized_window(&self) {
		self.did_resize.store(true, Ordering::Relaxed);
		self.window.handle_resize();

		let rebuilt = self
			.swapchain
			.write()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.rebuild(self.surface, self.window.size());

		if let Err(error) = rebuilt {
			raptor_core::log_fatal!(Render; "Could not rebuild the swapchain: {error}");
			panic!("Could not rebuild the swapchain: {error}");
		}

		let hook = self
			.resize_hook
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.clone();

		if let Some(hook) = hook {
			hook(self);
		}

		self.descriptors_rebuild_all();
	}

	pub fn descriptors_rebuild_all(&self) {
		let mut descriptors = self.descriptors();

		// SAFETY: the device is idle after the swapchain rebuild, and the layout cache is the one
		// the sets' layouts were requested from.
		if let Err(error) = unsafe { descriptors.rebuild_all(self.device(), &self.ds_layouts) } {
			raptor_core::log_error!(Render; "Could not rebuild the descriptor sets: {error:?}");
		}
	}

	pub fn begin_commands(&self) {
		let frame = self.frame();

		frame.cmd.reset(self.device());

		if let Err(error) = frame.cmd.begin(self.device()) {
			raptor_core::log_fatal!(Render; "Failed to begin recording command buffer: {error:?}");
			panic!("Failed to begin recording command buffer: {error:?}");
		}

		self.profiler_begin_frame();
	}

	pub fn present_frame(&self) -> FrameResult {
		let frame = self.frame();

		if let Err(error) = frame.cmd.end(self.device()) {
			raptor_core::log_fatal!(Render; "Failed to end the frame command buffer: {error:?}");
			panic!("Failed to end the frame command buffer: {error:?}");
		}

		let upload = self.upload();
		let swapchain = self.swapchain().handle();

		// SAFETY: the frame's commands are recorded and not pending, and the transfer semaphore
		// is the upload context's timeline.
		let submitted = unsafe {
			self.frame_loop().submit_and_present(
				self.device(),
				swapchain,
				frame.cmd.raw(),
				upload.transfer_semaphore(),
				upload.transfer_count(),
			)
		};

		let result = match submitted {
			Err(error) => {
				raptor_core::log_fatal!(Render; "Error submitting draw buffer: {error:?}");
				panic!("Error submitting draw buffer: {error:?}");
			}
			Ok(
				vk::Result::SUCCESS
				| vk::Result::ERROR_OUT_OF_DATE_KHR
				| vk::Result::SUBOPTIMAL_KHR,
			) => FrameResult::Success,
			Ok(other) => {
				raptor_core::log_error!(Render; "Error submitting present queue. Status: {:x}", other.as_raw());
				FrameResult::RenderError
			}
		};

		self.did_resize.store(false, Ordering::Relaxed);

		self.frame_loop().end_frame();
		self.core.set_elapsed(self.elapsed_frame_count());

		result
	}

	pub fn submit_immediate_upload(
		&self,
		record: impl FnOnce(vk::CommandBuffer),
	) -> Result<(), vk::Result> {
		self.upload().immediate(record)
	}

	pub fn submit_one_time_cmd(
		&self,
		record: impl FnOnce(vk::CommandBuffer),
	) -> Result<(), vk::Result> {
		let device = self.device();
		let pool = self.frame().pool();

		let cmd = device.allocate_command_buffer(pool)?;

		let result = (|| {
			device.begin_command_buffer(cmd)?;
			record(cmd);
			device.end_command_buffer(cmd)?;

			// SAFETY: the command buffer is recorded and not pending.
			unsafe {
				device.queue_submit(
					raptor_gpu::QueueKind::Graphics,
					&[],
					&[cmd],
					&[],
					vk::Fence::null(),
				)
			}?;

			device.queue_wait_idle(raptor_gpu::QueueKind::Graphics)
		})();

		device.reset_command_buffer(cmd);

		// SAFETY: the queue is idle, so the command buffer is not pending.
		unsafe { device.free_command_buffer(pool, cmd) };

		result
	}

	pub fn cmd_push_constants(
		&self,
		cmd: vk::CommandBuffer,
		layout: vk::PipelineLayout,
		stages: vk::ShaderStageFlags,
		data: &[u8],
	) {
		// SAFETY: the command buffer is recording and the layout declares a matching range.
		unsafe {
			self.device()
				.raw()
				.cmd_push_constants(cmd, layout, stages, 0, data)
		};
	}

	pub fn profiler_begin_frame(&self) {
		if let Some(profiler) = self
			.profiler
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.as_mut()
		{
			// SAFETY: the frame command buffer is recording outside a render pass and the slot's
			// queries were read back when the frame began.
			unsafe {
				profiler.begin_frame(
					self.device(),
					self.frame().cmd.raw(),
					self.frame_number() as usize,
				)
			};
		}
	}

	pub fn mark_gpu(&self, marker: GpuMarker) {
		if let Some(profiler) = self
			.profiler
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.as_mut()
		{
			// SAFETY: the frame command buffer is recording and `begin_commands` ran.
			unsafe { profiler.mark(self.device(), self.frame().cmd.raw(), marker as usize) };
		}
	}

	pub fn profiler_read_results(&self, delta_seconds: f64) {
		if let Some(profiler) = self
			.profiler
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.as_mut()
		{
			profiler.read_results(self.device(), self.frame_number() as usize, delta_seconds);
		}
	}

	pub fn gpu_ms(&self, marker: GpuMarker) -> f64 {
		self.profiler
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.as_ref()
			.map_or(0.0, |profiler| profiler.average_ms(marker as usize))
	}

	pub fn gpu_total_ms(&self) -> f64 {
		self.profiler
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.as_ref()
			.map_or(0.0, |profiler| profiler.total_ms())
	}

	pub fn profiler_enabled(&self) -> bool {
		self.profiler
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.is_some()
	}

	pub fn wait_idle(&self) {
		self.core.wait_idle();
	}
}

impl Drop for Gfx {
	fn drop(&mut self) {
		self.core.wait_idle();

		if let Some(buffers) = self.buffers.take() {
			buffers.destroy();
		}

		self.textures.release_all();

		if let Some(image) = self.dfg_lut.take() {
			image.destroy_now();
		}

		if let Some(image) = self.noise_texture.take() {
			image.destroy_now();
		}

		self.upload = None;

		if let Some(profiler) = self
			.profiler
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.take()
		{
			// SAFETY: the device is idle, so nothing uses the query pools.
			unsafe { profiler.destroy(self.core.device()) };
		}

		if let Some(frame_loop) = self.frame_loop.take() {
			// SAFETY: the device is idle, and the loop was made with it.
			unsafe { FrameLoop::destroy(*frame_loop, self.core.device()) };
		}

		for frame in self.frames.drain(..) {
			// SAFETY: the device is idle, so the command buffer is not pending.
			unsafe {
				self.core
					.device()
					.free_command_buffer(frame.pool, frame.cmd.raw());
				self.core.device().destroy_command_pool(frame.pool);
			}
		}

		let descriptors = std::mem::take(&mut *self.descriptors());

		// SAFETY: the device is idle and the descriptor sets are not in use.
		unsafe { descriptors.destroy(self.core.device(), self.core.allocator()) };

		// SAFETY: as above, for the layouts and samplers.
		unsafe {
			self.ds_layouts.destroy(self.core.device());
			self.samplers.destroy(self.core.device());
		}

		self.core.flush_deletions();

		self.swapchain
			.get_mut()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
			.destroy();

		self.window
			.destroy_surface(self.core.instance().handle(), self.surface);

		raptor_core::log_info!(Render; "Graphics backend destroyed ({:?})", Level::Info);
	}
}
