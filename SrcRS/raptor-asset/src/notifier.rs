use std::sync::{Condvar, Mutex, MutexGuard};

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T>
{
	mutex.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

#[derive(Default)]
struct SignalState
{
	signalled: bool,
	killed: bool,
}

/// A signal that stays up until it is reset, for one thread to wait on and another to raise
#[derive(Default)]
pub struct Notifier
{
	state: Mutex<SignalState>,
	changed: Condvar,
}

impl Notifier
{
	/// Raises the signal. A killed notifier ignores it.
	pub fn signal(&self)
	{
		let mut state = lock(&self.state);

		if state.killed {
			return;
		}

		state.signalled = true;
		self.changed.notify_all();
	}

	/// Waits until the signal is up, or returns at once if it already is
	pub fn wait(&self)
	{
		let mut state = lock(&self.state);

		while !state.signalled {
			state = self
				.changed
				.wait(state)
				.unwrap_or_else(|poisoned| poisoned.into_inner());
		}
	}

	pub fn reset(&self)
	{
		let mut state = lock(&self.state);

		state.signalled = false;
		state.killed = false;
	}

	/// Raises the signal for good, which wakes whatever is waiting
	pub fn kill(&self)
	{
		let mut state = lock(&self.state);

		state.killed = true;
		state.signalled = true;
		self.changed.notify_all();
	}

	pub fn is_signalled(&self) -> bool
	{
		lock(&self.state).signalled
	}

	pub fn is_killed(&self) -> bool
	{
		lock(&self.state).killed
	}
}

#[derive(Default)]
struct CountState
{
	signals: i32,
	killed: bool,
}

/// Counts the signals raised, so that each wait takes one of them
#[derive(Default)]
pub struct CountedNotifier
{
	state: Mutex<CountState>,
	changed: Condvar,
}

const KILLED_SIGNALS: i32 = 1000;

impl CountedNotifier
{
	pub fn signal(&self)
	{
		let mut state = lock(&self.state);

		if state.killed {
			return;
		}

		state.signals += 1;
		self.changed.notify_all();
	}

	/// Waits for a signal and takes it
	pub fn wait(&self)
	{
		let mut state = lock(&self.state);

		while state.signals == 0 {
			state = self
				.changed
				.wait(state)
				.unwrap_or_else(|poisoned| poisoned.into_inner());
		}

		state.signals -= 1;
	}

	/// Takes up to `count` signals without waiting, and leaves none behind if there are fewer
	pub fn discard(&self, count: i32)
	{
		if count <= 0 {
			return;
		}

		let mut state = lock(&self.state);

		state.signals = (state.signals - count).max(0);
	}

	pub fn kill(&self)
	{
		let mut state = lock(&self.state);

		state.killed = true;
		state.signals = KILLED_SIGNALS;
		self.changed.notify_all();
	}

	pub fn signal_count(&self) -> i32
	{
		lock(&self.state).signals
	}
}

#[cfg(test)]
mod tests
{
	use super::*;
	use std::sync::Arc;
	use std::thread;
	use std::time::Duration;

	#[test]
	fn a_signal_raised_before_the_wait_is_not_lost()
	{
		let notifier = Notifier::default();

		notifier.signal();
		notifier.wait();

		assert!(notifier.is_signalled());
	}

	#[test]
	fn a_wait_ends_when_another_thread_signals()
	{
		let notifier = Arc::new(Notifier::default());
		let waiter = Arc::clone(&notifier);

		let handle = thread::spawn(move || waiter.wait());

		thread::sleep(Duration::from_millis(20));
		notifier.signal();

		handle.join().unwrap();
	}

	#[test]
	fn a_reset_puts_the_signal_down()
	{
		let notifier = Notifier::default();

		notifier.signal();
		notifier.reset();

		assert!(!notifier.is_signalled() && !notifier.is_killed());
	}

	#[test]
	fn a_killed_notifier_wakes_waiters_and_ignores_signals()
	{
		let notifier = Arc::new(Notifier::default());
		let waiter = Arc::clone(&notifier);

		let handle = thread::spawn(move || waiter.wait());

		thread::sleep(Duration::from_millis(20));
		notifier.kill();
		handle.join().unwrap();

		notifier.reset();
		notifier.kill();
		notifier.signal();

		assert!(notifier.is_killed());
	}

	#[test]
	fn each_wait_takes_one_counted_signal()
	{
		let notifier = CountedNotifier::default();

		notifier.signal();
		notifier.signal();
		notifier.wait();

		assert_eq!(notifier.signal_count(), 1);
	}

	#[test]
	fn discarding_takes_what_it_can_and_never_goes_below_zero()
	{
		let notifier = CountedNotifier::default();

		notifier.signal();
		notifier.signal();
		notifier.signal();

		notifier.discard(2);
		assert_eq!(notifier.signal_count(), 1);

		notifier.discard(5);
		assert_eq!(notifier.signal_count(), 0);

		notifier.discard(-3);
		notifier.discard(0);
		assert_eq!(notifier.signal_count(), 0);
	}

	#[test]
	fn a_killed_counted_notifier_lets_waits_through()
	{
		let notifier = CountedNotifier::default();

		notifier.kill();
		notifier.wait();
		notifier.wait();

		notifier.signal();
		assert!(notifier.signal_count() > 100);
	}
}
