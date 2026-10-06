use std::ffi::{CString, c_void};
use std::sync::Mutex;
use std::sync::atomic::{AtomicPtr, AtomicU64, Ordering};

use ash::vk::{self, Handle};
use raptor_gpu::{FrameLoop, QueueKind, SemaphoreKind, SubmitSignal};

use crate::gpu::RxGpuDevice;
use crate::gpu_buffers::RxBufferResource;
use crate::gpu_resources::RxGpuAllocator;

/// What records and submits the work that copies data to the GPU: the commands the asset thread
/// batches uploads in, the ones other threads upload with at once, and the timeline that the
/// frame waits on to see the batches.
pub struct UploadContext
{
	device: *const RxGpuDevice,
	allocator: *const RxGpuAllocator,
	frame_loop: AtomicPtr<FrameLoop>,
	family: u32,
	pool: vk::CommandPool,
	cmd: vk::CommandBuffer,
	fence: vk::Fence,
	immediate_pool: vk::CommandPool,
	immediate_cmd: vk::CommandBuffer,
	immediate_fence: vk::Fence,
	immediate_lock: Mutex<()>,
	transfer_semaphore: vk::Semaphore,
	transfer_count: AtomicU64,
}

// SAFETY: the device, allocator and frame loop outlive the context and are used from any thread,
// and the handles are only recorded to and submitted under the locks and fences of the context.
unsafe impl Send for UploadContext {}

// SAFETY: as above.
unsafe impl Sync for UploadContext {}

fn name(device: &raptor_gpu::Device, handle: u64, label: &str)
{
	if let Ok(label) = CString::new(label) {
		device.set_object_name(vk::ObjectType::COMMAND_BUFFER, handle, &label);
	}
}

impl UploadContext
{
	pub fn device(&self) -> &raptor_gpu::Device
	{
		// SAFETY: the device outlives the context.
		unsafe { &(*self.device).device }
	}

	/// # Safety
	///
	/// All three must outlive the context.
	pub unsafe fn new(
		device: *const RxGpuDevice,
		allocator: *const RxGpuAllocator,
		frame_loop: *const FrameLoop,
		family: u32,
	) -> Result<Self, vk::Result>
	{
		// SAFETY: guaranteed by the caller.
		let gpu = unsafe { &(*device).device };

		let pool = gpu.create_command_pool(family)?;
		let cmd = gpu.allocate_command_buffer(pool)?;
		let immediate_pool = gpu.create_command_pool(family)?;
		let immediate_cmd = gpu.allocate_command_buffer(immediate_pool)?;

		name(gpu, cmd.as_raw(), "Upload");
		name(gpu, immediate_cmd.as_raw(), "UploadImmediate");

		Ok(Self {
			device,
			allocator,
			frame_loop: AtomicPtr::new(frame_loop.cast_mut()),
			family,
			pool,
			cmd,
			fence: gpu.create_fence(true)?,
			immediate_pool,
			immediate_cmd,
			immediate_fence: gpu.create_fence(true)?,
			immediate_lock: Mutex::new(()),
			transfer_semaphore: gpu.create_semaphore(SemaphoreKind::Timeline)?,
			transfer_count: AtomicU64::new(0),
		})
	}

	pub fn allocator(&self) -> &raptor_gpu::Allocator
	{
		// SAFETY: the allocator outlives the context.
		unsafe { &(*self.allocator).0 }
	}

	pub fn family(&self) -> u32
	{
		self.family
	}

	pub fn cmd(&self) -> vk::CommandBuffer
	{
		self.cmd
	}

	pub fn immediate_cmd(&self) -> vk::CommandBuffer
	{
		self.immediate_cmd
	}

	pub fn transfer_semaphore(&self) -> vk::Semaphore
	{
		self.transfer_semaphore
	}

	pub fn transfer_count(&self) -> u64
	{
		self.transfer_count.load(Ordering::SeqCst)
	}

	/// The number of frames drawn so far, or zero once the frames are gone.
	pub fn frame(&self) -> u32
	{
		let frame_loop = self.frame_loop.load(Ordering::Acquire);

		if frame_loop.is_null() {
			return 0;
		}

		// SAFETY: the frame loop is alive until `forget_frames`.
		unsafe { &*frame_loop }
			.fields
			.elapsed
			.load(Ordering::Relaxed)
	}

	/// Lets go of the frame loop, which is about to be destroyed.
	pub fn forget_frames(&self)
	{
		self.frame_loop
			.store(std::ptr::null_mut(), Ordering::Release);
	}

	/// Waits for the last batch to finish, and starts recording the next.
	pub fn begin_upload(&self) -> Result<(), vk::Result>
	{
		let device = self.device();

		device.wait_fence(self.fence, u64::MAX)?;
		device.reset_fence(self.fence)?;
		device.begin_command_buffer(self.cmd)
	}

	/// Submits the batch, and waits until it is done.
	pub fn end_upload(&self) -> Result<(), vk::Result>
	{
		let device = self.device();

		device.end_command_buffer(self.cmd)?;

		let value = self.transfer_count() + 1;

		// SAFETY: the command buffer is recorded, and the fence unsignaled.
		unsafe {
			device.queue_submit(
				QueueKind::Transfer,
				&[],
				&[self.cmd],
				&[SubmitSignal {
					semaphore: self.transfer_semaphore,
					value,
				}],
				self.fence,
			)
		}?;

		self.transfer_count.store(value, Ordering::SeqCst);

		device.wait_fence(self.fence, u64::MAX)
	}

	pub fn wait_for_uploads(&self) -> Result<(), vk::Result>
	{
		self.device().wait_fence(self.fence, u64::MAX)
	}

	/// Records `record` into a command buffer, submits it and waits until it is done. One thread
	/// does this at a time.
	pub fn immediate(&self, record: impl FnOnce(vk::CommandBuffer)) -> Result<(), vk::Result>
	{
		let _guard = self
			.immediate_lock
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner());
		let device = self.device();

		device.begin_command_buffer(self.immediate_cmd)?;
		record(self.immediate_cmd);
		device.end_command_buffer(self.immediate_cmd)?;

		device.reset_fence(self.immediate_fence)?;

		// SAFETY: the command buffer is recorded, and the fence unsignaled.
		unsafe {
			device.queue_submit(
				QueueKind::Transfer,
				&[],
				&[self.immediate_cmd],
				&[],
				self.immediate_fence,
			)
		}?;

		device.wait_fence(self.immediate_fence, u64::MAX)?;
		device.reset_fence(self.immediate_fence)?;
		device.reset_command_buffer(self.immediate_cmd);

		Ok(())
	}

	/// Destroys a buffer once nothing submitted could still be using it.
	///
	/// # Safety
	///
	/// `resource` must come from `rx_buffer_detach` on this context's allocator.
	pub unsafe fn destroy_buffer(&self, resource: *mut RxBufferResource)
	{
		let _ = self.device().queue_wait_idle(QueueKind::Graphics);

		// SAFETY: guaranteed by the caller.
		unsafe {
			if !resource.is_null() {
				Box::from_raw(resource).destroy(&(*self.allocator).0);
			}
		}
	}

	pub fn destroy(&mut self)
	{
		let device = self.device();

		// SAFETY: nothing is pending when the context is destroyed.
		unsafe {
			device.free_command_buffer(self.pool, self.cmd);
			device.free_command_buffer(self.immediate_pool, self.immediate_cmd);
			device.destroy_command_pool(self.pool);
			device.destroy_command_pool(self.immediate_pool);
			device.destroy_fence(self.fence);
			device.destroy_fence(self.immediate_fence);
			device.destroy_semaphore(self.transfer_semaphore);
		}
	}
}

pub type RxUploadContext = UploadContext;

/// # Safety
///
/// The device, allocator and frame loop must outlive the context.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_context_new(
	device: *const RxGpuDevice,
	allocator: *const RxGpuAllocator,
	frame_loop: *const FrameLoop,
	family: u32,
) -> *mut RxUploadContext
{
	// SAFETY: guaranteed by the caller.
	match unsafe { UploadContext::new(device, allocator, frame_loop, family) } {
		Ok(context) => Box::into_raw(Box::new(context)),
		Err(_) => std::ptr::null_mut(),
	}
}

/// Destroys what the context made and frees it.
///
/// # Safety
///
/// `context` must be null or come from `rx_upload_context_new`, with nothing pending, and must not
/// be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_context_free(context: *mut RxUploadContext)
{
	if !context.is_null() {
		// SAFETY: guaranteed by the caller.
		let mut context = unsafe { Box::from_raw(context) };

		context.destroy();
	}
}

/// # Safety
///
/// `context` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_cmd(context: *const RxUploadContext) -> *mut c_void
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*context }.cmd().as_raw() as *mut c_void
}

/// # Safety
///
/// `context` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_family(context: *const RxUploadContext) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*context }.family()
}

/// # Safety
///
/// `context` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_transfer_semaphore(context: *const RxUploadContext) -> u64
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*context }.transfer_semaphore().as_raw()
}

/// # Safety
///
/// `context` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_transfer_count(context: *const RxUploadContext) -> u64
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*context }.transfer_count()
}

/// Calls `record` with a command buffer, then submits it and waits until it is done. Returns a
/// Vulkan result.
///
/// # Safety
///
/// `context` must be live and `record` safe to call with `user` and a recording command buffer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_immediate(
	context: *const RxUploadContext,
	record: unsafe extern "C" fn(user: *mut c_void, cmd: *mut c_void),
	user: *mut c_void,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let result = unsafe { &*context }.immediate(|cmd| {
		// SAFETY: guaranteed by the caller.
		unsafe { record(user, cmd.as_raw() as *mut c_void) };
	});

	match result {
		Ok(()) => vk::Result::SUCCESS.as_raw(),
		Err(error) => error.as_raw(),
	}
}

/// # Safety
///
/// `context` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_begin(context: *const RxUploadContext) -> i32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*context }
		.begin_upload()
		.map_or_else(|error| error.as_raw(), |()| vk::Result::SUCCESS.as_raw())
}

/// # Safety
///
/// `context` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_end(context: *const RxUploadContext) -> i32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*context }
		.end_upload()
		.map_or_else(|error| error.as_raw(), |()| vk::Result::SUCCESS.as_raw())
}

/// # Safety
///
/// `context` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_wait(context: *const RxUploadContext)
{
	// SAFETY: guaranteed by the caller.
	let _ = unsafe { &*context }.wait_for_uploads();
}

/// # Safety
///
/// `context` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_upload_forget_frames(context: *const RxUploadContext)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*context }.forget_frames();
}
