use std::ffi::{CString, c_void};
use std::sync::Arc;

use raptor_asset::scheduler::{
	Backend, Job, LoadStatus, LogLevel, Resource, Scheduler, SchedulerConfig,
};
use raptor_asset::ticket::{Callback, CallbackFn, DestroyFn, Ticket};

use crate::LogFn;

const LOG_INFO: i32 = 1;
const LOG_ERROR: i32 = 3;
const LOG_CATEGORY_ASSET: i32 = 5;

/// What the scheduler asks of the engine, as functions. `user` is passed back to each of them.
#[repr(C)]
pub struct RxAssetBackend
{
	pub user: *mut c_void,
	pub load: unsafe extern "C" fn(user: *mut c_void, job: *mut c_void) -> i32,
	pub begin_upload: unsafe extern "C" fn(user: *mut c_void),
	pub upload: unsafe extern "C" fn(user: *mut c_void, job: *mut c_void) -> u8,
	pub end_upload: unsafe extern "C" fn(user: *mut c_void),
	pub finish: unsafe extern "C" fn(user: *mut c_void, job: *mut c_void, status: i32),
	pub frame: unsafe extern "C" fn(user: *mut c_void) -> u32,
	pub wait_for_uploads: unsafe extern "C" fn(user: *mut c_void),
	pub destroy: unsafe extern "C" fn(user: *mut c_void, resource: *mut c_void),
	pub log: LogFn,
}

struct CBackend
{
	callbacks: RxAssetBackend,
}

// SAFETY: whoever supplies the functions promises they can be called from any thread.
unsafe impl Send for CBackend {}

// SAFETY: as above.
unsafe impl Sync for CBackend {}

impl Backend for CBackend
{
	fn load(&self, job: Job) -> LoadStatus
	{
		// SAFETY: guaranteed by whoever supplied the functions.
		let status = unsafe { (self.callbacks.load)(self.callbacks.user, job.0) };

		LoadStatus::from_u8(status as u8)
	}

	fn begin_upload(&self)
	{
		// SAFETY: as above.
		unsafe { (self.callbacks.begin_upload)(self.callbacks.user) };
	}

	fn upload(&self, job: Job) -> bool
	{
		// SAFETY: as above.
		unsafe { (self.callbacks.upload)(self.callbacks.user, job.0) != 0 }
	}

	fn end_upload(&self)
	{
		// SAFETY: as above.
		unsafe { (self.callbacks.end_upload)(self.callbacks.user) };
	}

	fn finish(&self, job: Job, status: LoadStatus)
	{
		// SAFETY: as above.
		unsafe { (self.callbacks.finish)(self.callbacks.user, job.0, i32::from(status as u8)) };
	}

	fn frame(&self) -> u32
	{
		// SAFETY: as above.
		unsafe { (self.callbacks.frame)(self.callbacks.user) }
	}

	fn wait_for_uploads(&self)
	{
		// SAFETY: as above.
		unsafe { (self.callbacks.wait_for_uploads)(self.callbacks.user) };
	}

	fn destroy(&self, resource: Resource)
	{
		// SAFETY: as above.
		unsafe { (self.callbacks.destroy)(self.callbacks.user, resource.0) };
	}

	fn log(&self, level: LogLevel, message: &str)
	{
		let text = CString::new(message.replace('\0', "")).unwrap_or_default();

		// SAFETY: as above.
		unsafe {
			(self.callbacks.log)(
				self.callbacks.user,
				match level {
					LogLevel::Info => LOG_INFO,
					LogLevel::Error => LOG_ERROR,
				},
				LOG_CATEGORY_ASSET,
				text.as_ptr(),
				text.as_bytes().len(),
			);
		}
	}
}

pub type RxAssetScheduler = Scheduler;

/// Starts the asset threads: a manager and `workers` workers.
///
/// # Safety
///
/// `backend` must point at a valid `RxAssetBackend` whose functions can be called from any thread
/// until the scheduler is freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_asset_scheduler_new(
	backend: *const RxAssetBackend,
	workers: u32,
) -> *mut RxAssetScheduler
{
	// SAFETY: guaranteed by the caller.
	let callbacks = unsafe { std::ptr::read(backend) };

	Box::into_raw(Box::new(Scheduler::new(
		Box::new(CBackend { callbacks }),
		SchedulerConfig::new(workers),
	)))
}

/// Stops the threads, if they are not stopped, and frees the scheduler.
///
/// # Safety
///
/// `scheduler` must be null or come from `rx_asset_scheduler_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_asset_scheduler_free(scheduler: *mut RxAssetScheduler)
{
	if !scheduler.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(scheduler) });
	}
}

/// Queues a job to be loaded.
///
/// # Safety
///
/// `scheduler` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_asset_scheduler_submit(scheduler: *const RxAssetScheduler, job: *mut c_void)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*scheduler }.submit(Job(job));
}

/// Queues a resource to be destroyed `frame_spacing` frames from now.
///
/// # Safety
///
/// `scheduler` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_asset_scheduler_delete_resource(
	scheduler: *const RxAssetScheduler,
	resource: *mut c_void,
	frame_spacing: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*scheduler }.delete_resource(Resource(resource), frame_spacing);
}

/// Wakes the manager.
///
/// # Safety
///
/// `scheduler` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_asset_scheduler_signal(scheduler: *const RxAssetScheduler)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*scheduler }.signal();
}

/// Stops the threads and waits for them to end.
///
/// # Safety
///
/// `scheduler` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_asset_scheduler_stop(scheduler: *const RxAssetScheduler)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*scheduler }.stop();
}

/// Destroys every resource that is waiting, due or not.
///
/// # Safety
///
/// `scheduler` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_asset_scheduler_flush_deletions(scheduler: *const RxAssetScheduler)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*scheduler }.flush_deletions();
}

pub type RxTicket = Ticket;

/// Makes a ticket, with one reference.
#[unsafe(no_mangle)]
pub extern "C" fn rx_ticket_new() -> *const RxTicket
{
	Arc::into_raw(Arc::new(Ticket::default()))
}

/// Takes another reference to a ticket.
///
/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_retain(ticket: *const RxTicket)
{
	// SAFETY: guaranteed by the caller.
	unsafe { Arc::increment_strong_count(ticket) };
}

/// Gives up a reference to a ticket, which is freed with the last.
///
/// # Safety
///
/// `ticket` must be live, and not used again through this reference.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_release(ticket: *const RxTicket)
{
	// SAFETY: guaranteed by the caller.
	unsafe { Arc::decrement_strong_count(ticket) };
}

/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_is_loaded(ticket: *const RxTicket) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*ticket }.is_loaded())
}

/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_wait_finished(ticket: *const RxTicket)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*ticket }.wait_finished();
}

/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_wait_uploaded(ticket: *const RxTicket)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*ticket }.wait_uploaded();
}

/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_signal_finished(ticket: *const RxTicket)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*ticket }.signal_finished();
}

/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_signal_uploaded(ticket: *const RxTicket)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*ticket }.signal_uploaded();
}

/// For an asset that needs no loading.
///
/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_mark_loaded(ticket: *const RxTicket)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*ticket }.mark_loaded();
}

/// Calls `callback(user, asset)` once the asset has loaded, at once if it already has. `destroy(user)`
/// follows, once, whether or not the callback was called.
///
/// # Safety
///
/// `ticket` must be live, and the callback functions callable from any thread.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_on_loaded(
	ticket: *const RxTicket,
	asset: *mut c_void,
	callback: CallbackFn,
	user: *mut c_void,
	destroy: Option<DestroyFn>,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (&*ticket).on_loaded(asset, Callback::new(callback, user, destroy)) };
}

/// Calls `callback(user, null)` if the asset fails to load.
///
/// # Safety
///
/// As for `rx_ticket_on_loaded`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_on_error(
	ticket: *const RxTicket,
	callback: CallbackFn,
	user: *mut c_void,
	destroy: Option<DestroyFn>,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { (&*ticket).on_error(Callback::new(callback, user, destroy)) };
}

/// The asset has loaded and its upload is done.
///
/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_complete(
	ticket: *const RxTicket,
	asset: *mut c_void,
	run_callbacks: u8,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*ticket }.complete(asset, run_callbacks != 0);
}

/// The asset could not be loaded.
///
/// # Safety
///
/// `ticket` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_ticket_fail(ticket: *const RxTicket, run_error_callback: u8)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*ticket }.fail(run_error_callback != 0);
}

pub const RX_LOAD_STATUS_NONE: i32 = LoadStatus::None as i32;
pub const RX_LOAD_STATUS_SUCCESS: i32 = LoadStatus::Success as i32;
pub const RX_LOAD_STATUS_ERROR: i32 = LoadStatus::Error as i32;

