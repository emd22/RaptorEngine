use std::ffi::c_void;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Condvar, Mutex, MutexGuard};

use crate::notifier::Notifier;

pub type CallbackFn = unsafe extern "C" fn(user: *mut c_void, argument: *mut c_void);
pub type DestroyFn = unsafe extern "C" fn(user: *mut c_void);

enum Kind {
	Foreign {
		call: CallbackFn,
		destroy: Option<DestroyFn>,
		user: *mut c_void,
	},
	Closure(Option<Box<dyn FnOnce() + Send>>),
}

/// Something to call once, along with what to do with its data afterwards
pub struct Callback {
	kind: Kind,
}

// SAFETY: whoever makes a foreign callback promises it can run on any thread, and closures are
// `Send`.
unsafe impl Send for Callback {}

impl Callback {
	/// # Safety
	///
	/// `call` must be callable from any thread with `user` and one argument, and `destroy` with `user`
	/// once.
	pub unsafe fn new(call: CallbackFn, user: *mut c_void, destroy: Option<DestroyFn>) -> Self {
		Self {
			kind: Kind::Foreign {
				call,
				destroy,
				user,
			},
		}
	}

	pub fn from_fn(call: impl FnOnce() + Send + 'static) -> Self {
		Self {
			kind: Kind::Closure(Some(Box::new(call))),
		}
	}

	fn invoke(mut self, argument: *mut c_void) {
		match &mut self.kind {
			// SAFETY: guaranteed by `new`.
			Kind::Foreign { call, user, .. } => unsafe { call(*user, argument) },
			Kind::Closure(call) => {
				if let Some(call) = call.take() {
					call();
				}
			}
		}
	}
}

impl Drop for Callback {
	fn drop(&mut self) {
		if let Kind::Foreign {
			destroy: Some(destroy),
			user,
			..
		} = self.kind
		{
			// SAFETY: guaranteed by `new`; a callback is dropped once.
			unsafe { destroy(user) };
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Outcome {
	Pending,
	Loaded,
	Failed,
}

struct Callbacks {
	outcome: Outcome,
	on_loaded: Vec<Callback>,
	on_error: Option<Callback>,
}

/// What an asset's loading shares with whoever asked for it. It says when the asset is ready, and holds
/// what is to be done then.
pub struct Ticket {
	finished: Notifier,
	uploaded: (Mutex<bool>, Condvar),
	loaded: AtomicBool,
	callbacks: Mutex<Callbacks>,
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
	mutex
		.lock()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
}

impl Default for Ticket {
	fn default() -> Self {
		Self {
			finished: Notifier::default(),
			uploaded: (Mutex::new(false), Condvar::new()),
			loaded: AtomicBool::new(false),
			callbacks: Mutex::new(Callbacks {
				outcome: Outcome::Pending,
				on_loaded: Vec::new(),
				on_error: None,
			}),
		}
	}
}

impl Ticket {
	/// A ticket for an asset that needs no loading
	pub fn loaded() -> std::sync::Arc<Self> {
		let ticket = std::sync::Arc::new(Self::default());
		ticket.mark_loaded();

		ticket
	}

	pub fn is_loaded(&self) -> bool {
		self.loaded.load(Ordering::SeqCst)
	}

	pub fn is_finished(&self) -> bool {
		self.finished.is_signalled()
	}

	pub fn is_uploaded(&self) -> bool {
		*lock(&self.uploaded.0)
	}

	/// Waits until the loading has finished, successfully or not
	pub fn wait_finished(&self) {
		self.finished.wait();
	}

	pub fn signal_finished(&self) {
		self.finished.signal();
	}

	pub fn signal_uploaded(&self) {
		*lock(&self.uploaded.0) = true;
		self.uploaded.1.notify_all();
	}

	pub fn wait_uploaded(&self) {
		let mut uploaded = lock(&self.uploaded.0);

		while !*uploaded {
			uploaded = self
				.uploaded
				.1
				.wait(uploaded)
				.unwrap_or_else(|poisoned| poisoned.into_inner());
		}
	}

	/// For an asset that needs no loading: it is uploaded, loaded and finished at once. What was waiting
	/// to be told about it is dropped.
	pub fn mark_loaded(&self) {
		if self.is_loaded() {
			return;
		}

		self.settle(Outcome::Loaded);

		self.signal_uploaded();
		self.loaded.store(true, Ordering::SeqCst);
		self.finished.signal();
	}

	fn settle(&self, outcome: Outcome) -> Callbacks {
		let mut callbacks = lock(&self.callbacks);

		let taken = Callbacks {
			outcome: callbacks.outcome,
			on_loaded: std::mem::take(&mut callbacks.on_loaded),
			on_error: callbacks.on_error.take(),
		};

		callbacks.outcome = outcome;

		taken
	}

	/// Calls `callback` with `asset` once the asset has loaded, at once if it already has. If it fails
	/// to load the callback is dropped.
	pub fn on_loaded(&self, asset: *mut c_void, callback: Callback) {
		let mut callbacks = lock(&self.callbacks);

		match callbacks.outcome {
			Outcome::Pending => callbacks.on_loaded.push(callback),
			Outcome::Loaded => {
				drop(callbacks);
				callback.invoke(asset);
			}
			Outcome::Failed => {}
		}
	}

	/// Runs `call` once the asset has loaded, at once if it already has. If it fails to load `call` is
	/// dropped.
	pub fn then(&self, call: impl FnOnce() + Send + 'static) {
		self.on_loaded(std::ptr::null_mut(), Callback::from_fn(call));
	}

	/// Runs `call` if the asset fails to load, at once if it already has.
	pub fn or_else(&self, call: impl FnOnce() + Send + 'static) {
		self.on_error(Callback::from_fn(call));
	}

	/// Calls `callback` if the asset fails to load, at once if it already has. A ticket has one, and a
	/// new one replaces the old.
	pub fn on_error(&self, callback: Callback) {
		let mut callbacks = lock(&self.callbacks);

		match callbacks.outcome {
			Outcome::Pending => callbacks.on_error = Some(callback),
			Outcome::Failed => {
				drop(callbacks);
				callback.invoke(std::ptr::null_mut());
			}
			Outcome::Loaded => {}
		}
	}

	/// The asset has loaded and its upload is done. Calls what was waiting for it, with `asset` if
	/// `run_callbacks` is set, and then marks the ticket loaded.
	pub fn complete(&self, asset: *mut c_void, run_callbacks: bool) {
		let taken = self.settle(Outcome::Loaded);

		let taken = if run_callbacks {
			taken.on_loaded
		} else {
			Vec::new()
		};

		for callback in taken {
			callback.invoke(asset);
		}

		self.loaded.store(true, Ordering::SeqCst);
		self.finished.signal();
		self.signal_uploaded();
	}

	/// The asset could not be loaded. Finishes the ticket, and calls the error callback if there is
	/// one and `run_error_callback` is set.
	pub fn fail(&self, run_error_callback: bool) {
		let taken = self.settle(Outcome::Failed);

		self.finished.signal();

		if run_error_callback && let Some(callback) = taken.on_error {
			callback.invoke(std::ptr::null_mut());
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::Arc;
	use std::sync::atomic::AtomicUsize;
	use std::thread;
	use std::time::Duration;

	struct Counter {
		calls: AtomicUsize,
		last_argument: AtomicUsize,
		destroyed: AtomicUsize,
	}

	unsafe extern "C" fn count(user: *mut c_void, argument: *mut c_void) {
		// SAFETY: `user` is a `Counter` that outlives the test.
		let counter = unsafe { &*(user as *const Counter) };

		counter.calls.fetch_add(1, Ordering::SeqCst);
		counter
			.last_argument
			.store(argument as usize, Ordering::SeqCst);
	}

	unsafe extern "C" fn destroyed(user: *mut c_void) {
		// SAFETY: as above.
		unsafe { &*(user as *const Counter) }
			.destroyed
			.fetch_add(1, Ordering::SeqCst);
	}

	fn counter() -> Arc<Counter> {
		Arc::new(Counter {
			calls: AtomicUsize::new(0),
			last_argument: AtomicUsize::new(0),
			destroyed: AtomicUsize::new(0),
		})
	}

	fn callback(counter: &Arc<Counter>) -> Callback {
		// SAFETY: the counter is kept alive by the test.
		unsafe { Callback::new(count, Arc::as_ptr(counter) as *mut c_void, Some(destroyed)) }
	}

	#[test]
	fn a_new_ticket_is_not_loaded_or_finished() {
		let ticket = Ticket::default();

		assert!(!ticket.is_loaded() && !ticket.is_finished() && !ticket.is_uploaded());
	}

	#[test]
	fn waiting_callbacks_are_called_with_the_asset_once_it_completes() {
		let ticket = Ticket::default();
		let counter = counter();

		ticket.on_loaded(std::ptr::null_mut(), callback(&counter));
		ticket.on_loaded(std::ptr::null_mut(), callback(&counter));

		assert_eq!(counter.calls.load(Ordering::SeqCst), 0);

		ticket.complete(0x1234 as *mut c_void, true);

		assert_eq!(counter.calls.load(Ordering::SeqCst), 2);
		assert_eq!(counter.last_argument.load(Ordering::SeqCst), 0x1234);
		assert_eq!(counter.destroyed.load(Ordering::SeqCst), 2);
		assert!(ticket.is_loaded() && ticket.is_finished() && ticket.is_uploaded());
	}

	#[test]
	fn a_callback_added_after_completion_is_called_at_once_with_the_asset_it_is_given() {
		let ticket = Ticket::default();
		let counter = counter();

		ticket.complete(std::ptr::null_mut(), true);
		ticket.on_loaded(0x55 as *mut c_void, callback(&counter));

		assert_eq!(counter.calls.load(Ordering::SeqCst), 1);
		assert_eq!(counter.last_argument.load(Ordering::SeqCst), 0x55);
	}

	#[test]
	fn completing_without_callbacks_leaves_them_uncalled_but_the_ticket_loaded() {
		let ticket = Ticket::default();
		let counter = counter();

		ticket.on_loaded(std::ptr::null_mut(), callback(&counter));
		ticket.complete(std::ptr::null_mut(), false);

		assert!(ticket.is_loaded());
		assert_eq!(counter.calls.load(Ordering::SeqCst), 0);
		assert_eq!(counter.destroyed.load(Ordering::SeqCst), 1);
	}

	#[test]
	fn a_ticket_marked_loaded_calls_later_callbacks_at_once_and_drops_earlier_ones() {
		let ticket = Ticket::default();
		let early = counter();
		let late = counter();

		ticket.on_loaded(std::ptr::null_mut(), callback(&early));
		ticket.mark_loaded();
		ticket.on_loaded(std::ptr::null_mut(), callback(&late));

		assert!(ticket.is_loaded() && ticket.is_uploaded() && ticket.is_finished());
		assert_eq!(early.calls.load(Ordering::SeqCst), 0);
		assert_eq!(early.destroyed.load(Ordering::SeqCst), 1);
		assert_eq!(late.calls.load(Ordering::SeqCst), 1);
	}

	#[test]
	fn a_failed_ticket_calls_the_error_callback_and_drops_the_loaded_ones() {
		let ticket = Ticket::default();
		let loaded = counter();
		let error = counter();

		ticket.on_loaded(std::ptr::null_mut(), callback(&loaded));
		ticket.on_error(callback(&error));
		ticket.fail(true);

		assert!(ticket.is_finished() && !ticket.is_loaded());
		assert_eq!(error.calls.load(Ordering::SeqCst), 1);
		assert_eq!(loaded.calls.load(Ordering::SeqCst), 0);
		assert_eq!(loaded.destroyed.load(Ordering::SeqCst), 1);
	}

	#[test]
	fn an_error_callback_added_to_a_failed_ticket_is_called_at_once_and_one_added_to_a_loaded_ticket_never()
	 {
		let failed = Ticket::default();
		let loaded = Ticket::default();
		let on_failed = counter();
		let on_loaded = counter();

		failed.fail(true);
		failed.on_error(callback(&on_failed));

		loaded.complete(std::ptr::null_mut(), true);
		loaded.on_error(callback(&on_loaded));

		assert_eq!(on_failed.calls.load(Ordering::SeqCst), 1);
		assert_eq!(on_loaded.calls.load(Ordering::SeqCst), 0);
	}

	#[test]
	fn the_error_callback_can_be_held_back() {
		let ticket = Ticket::default();
		let error = counter();

		ticket.on_error(callback(&error));
		ticket.fail(false);

		assert_eq!(error.calls.load(Ordering::SeqCst), 0);
		assert!(ticket.is_finished());
	}

	#[test]
	fn a_loaded_callback_added_to_a_failed_ticket_is_dropped_without_being_called() {
		let ticket = Ticket::default();
		let counter = counter();

		ticket.fail(true);
		ticket.on_loaded(std::ptr::null_mut(), callback(&counter));

		assert_eq!(counter.calls.load(Ordering::SeqCst), 0);
		assert_eq!(counter.destroyed.load(Ordering::SeqCst), 1);
	}

	#[test]
	fn waiting_for_the_finish_lasts_until_another_thread_completes_the_ticket() {
		let ticket = Arc::new(Ticket::default());
		let waiter = Arc::clone(&ticket);

		let handle = thread::spawn(move || {
			waiter.wait_finished();
			waiter.is_loaded()
		});

		thread::sleep(Duration::from_millis(20));
		ticket.complete(std::ptr::null_mut(), true);

		assert!(handle.join().unwrap());
	}

	#[test]
	fn waiting_for_the_upload_lasts_until_it_is_signalled() {
		let ticket = Arc::new(Ticket::default());
		let waiter = Arc::clone(&ticket);

		let handle = thread::spawn(move || waiter.wait_uploaded());

		thread::sleep(Duration::from_millis(20));
		ticket.signal_uploaded();

		handle.join().unwrap();
		assert!(ticket.is_uploaded());
	}

	#[test]
	fn callbacks_added_while_completing_are_never_lost() {
		for _ in 0..200 {
			let ticket = Arc::new(Ticket::default());
			let counter = counter();

			let adder = {
				let ticket = Arc::clone(&ticket);
				let counter = Arc::clone(&counter);

				thread::spawn(move || ticket.on_loaded(0x9 as *mut c_void, callback(&counter)))
			};

			ticket.complete(0x9 as *mut c_void, true);
			adder.join().unwrap();

			assert_eq!(counter.calls.load(Ordering::SeqCst), 1);
		}
	}
}
