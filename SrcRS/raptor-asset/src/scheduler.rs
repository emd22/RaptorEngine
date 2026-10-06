use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::notifier::{CountedNotifier, Notifier};

pub const MAX_WORKERS: u32 = 10;

/// How long the manager goes without anything to do before it sleeps until it is woken
pub const IDLE_SLEEP_AFTER: Duration = Duration::from_secs(3);

/// How long the manager rests between looks for work while it is not asleep
pub const IDLE_POLL: Duration = Duration::from_millis(20);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum LoadStatus {
	None = 0,
	Success = 1,
	Error = 2,
}

impl LoadStatus {
	pub fn from_u8(value: u8) -> Self {
		match value {
			1 => Self::Success,
			2 => Self::Error,
			_ => Self::None,
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogLevel {
	Info,
	Error,
}

/// What the scheduler asks of the engine
pub trait Backend: Send + Sync + 'static {
	/// An item of work, which the backend knows how to load, upload and finish. The scheduler only
	/// passes it around.
	type Job: Send + 'static;

	/// Something to be destroyed once the frames that could use it are over
	type Resource: Send + 'static;

	/// Loads a job on a worker thread
	fn load(&self, job: &mut Self::Job) -> LoadStatus;

	/// Starts a batch of uploads
	fn begin_upload(&self);

	/// Uploads a job that loaded. Says whether that was an upload.
	fn upload(&self, job: &mut Self::Job) -> bool;

	/// Sends the batch of uploads off and waits until it is done
	fn end_upload(&self);

	/// A job is done, whether it loaded, failed or never got a status
	fn finish(&self, job: Self::Job, status: LoadStatus);

	/// The number of frames drawn so far
	fn frame(&self) -> u32;

	/// Waits until nothing that was uploading could still be using what is about to be destroyed
	fn wait_for_uploads(&self);

	fn destroy(&self, resource: Self::Resource);

	fn log(&self, level: LogLevel, message: &str);
}

struct Deletion<R> {
	resource: R,
	min_frame: u32,
}

impl<R> Deletion<R> {
	/// Whether `current_frame` has reached the minimum frame, with both counting around past the top of
	/// the range
	fn is_due(&self, current_frame: u32) -> bool {
		(current_frame.wrapping_sub(self.min_frame) as i32) >= 0
	}
}

struct Worker<J> {
	busy: AtomicBool,
	pending_upload: AtomicBool,
	status: AtomicU8,
	job: Mutex<Option<J>>,
	ready: Notifier,
	running: AtomicBool,
}

impl<J> Worker<J> {
	fn new() -> Self {
		Self {
			busy: AtomicBool::new(false),
			pending_upload: AtomicBool::new(false),
			status: AtomicU8::new(LoadStatus::None as u8),
			job: Mutex::new(None),
			ready: Notifier::default(),
			running: AtomicBool::new(true),
		}
	}

	fn status(&self) -> LoadStatus {
		LoadStatus::from_u8(self.status.load(Ordering::SeqCst))
	}
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
	mutex
		.lock()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
}

struct Shared<B: Backend> {
	backend: B,
	queue: Mutex<VecDeque<B::Job>>,
	deletions: Mutex<VecDeque<Deletion<B::Resource>>>,
	manager_signal: CountedNotifier,
	active: AtomicBool,
	workers: Vec<Worker<B::Job>>,
	idle_sleep_after: Duration,
	idle_poll: Duration,
}

pub struct SchedulerConfig {
	pub workers: u32,
	pub idle_sleep_after: Duration,
	pub idle_poll: Duration,
}

impl SchedulerConfig {
	pub fn new(workers: u32) -> Self {
		Self {
			workers,
			idle_sleep_after: IDLE_SLEEP_AFTER,
			idle_poll: IDLE_POLL,
		}
	}
}

/// Loads assets on worker threads, uploads what they loaded in batches, and destroys resources once the
/// frames that could be using them are over, all from a manager thread.
pub struct Scheduler<B: Backend> {
	shared: Arc<Shared<B>>,
	manager: Mutex<Option<JoinHandle<()>>>,
	workers: Mutex<Vec<JoinHandle<()>>>,
}

impl<B: Backend> Scheduler<B> {
	pub fn new(backend: B, config: SchedulerConfig) -> Self {
		let worker_count = config.workers.min(MAX_WORKERS) as usize;

		let shared = Arc::new(Shared {
			backend,
			queue: Mutex::new(VecDeque::new()),
			deletions: Mutex::new(VecDeque::new()),
			manager_signal: CountedNotifier::default(),
			active: AtomicBool::new(true),
			workers: (0..worker_count).map(|_| Worker::new()).collect(),
			idle_sleep_after: config.idle_sleep_after,
			idle_poll: config.idle_poll,
		});

		let workers = (0..worker_count)
			.map(|index| {
				let shared = Arc::clone(&shared);

				thread::Builder::new()
					.name(format!("AssetWorker_{index}"))
					.spawn(move || shared.worker_loop(index))
					.expect("a worker thread could be started")
			})
			.collect();

		let manager = {
			let shared = Arc::clone(&shared);

			thread::Builder::new()
				.name("AssetManager".to_owned())
				.spawn(move || shared.manager_loop())
				.expect("the manager thread could be started")
		};

		Self {
			shared,
			manager: Mutex::new(Some(manager)),
			workers: Mutex::new(workers),
		}
	}

	pub fn backend(&self) -> &B {
		&self.shared.backend
	}

	/// Queues a job to be loaded
	pub fn submit(&self, job: B::Job) {
		lock(&self.shared.queue).push_back(job);
		self.signal();
	}

	/// Queues a resource to be destroyed `frame_spacing` frames from now
	pub fn delete_resource(&self, resource: B::Resource, frame_spacing: u32) {
		let min_frame = self.shared.backend.frame().wrapping_add(frame_spacing);

		lock(&self.shared.deletions).push_back(Deletion {
			resource,
			min_frame,
		});
		self.signal();
	}

	/// Wakes the manager
	pub fn signal(&self) {
		self.shared.manager_signal.signal();
	}

	/// Stops the manager and the workers and waits for them to end
	pub fn stop(&self) {
		self.shared.active.store(false, Ordering::SeqCst);
		self.shared.manager_signal.kill();

		for worker in &self.shared.workers {
			worker.running.store(false, Ordering::SeqCst);
			worker.ready.kill();
		}

		for handle in lock(&self.workers).drain(..) {
			let _ = handle.join();
		}

		if let Some(handle) = lock(&self.manager).take() {
			let _ = handle.join();
		}
	}

	/// Destroys every resource that is waiting, due or not
	pub fn flush_deletions(&self) {
		loop {
			let Some(deletion) = lock(&self.shared.deletions).pop_front() else {
				break;
			};

			self.shared.backend.destroy(deletion.resource);
		}
	}

	pub fn queued_jobs(&self) -> usize {
		lock(&self.shared.queue).len()
	}

	pub fn queued_deletions(&self) -> usize {
		lock(&self.shared.deletions).len()
	}
}

impl<B: Backend> Drop for Scheduler<B> {
	fn drop(&mut self) {
		self.stop();
	}
}

impl<B: Backend> Shared<B> {
	fn worker_loop(&self, index: usize) {
		let worker = &self.workers[index];

		while worker.running.load(Ordering::SeqCst) {
			worker.ready.wait();

			if !worker.running.load(Ordering::SeqCst) || worker.ready.is_killed() {
				break;
			}

			worker.ready.reset();

			let status = {
				let mut slot = lock(&worker.job);

				let Some(job) = slot.as_mut() else {
					continue;
				};

				self.backend.load(job)
			};

			worker.status.store(status as u8, Ordering::SeqCst);
			worker.pending_upload.store(true, Ordering::SeqCst);

			self.manager_signal.signal();
		}
	}

	/// Uploads what the workers have loaded, in one batch. Returns how many were uploaded.
	fn check_for_uploadable_data(&self) -> i32 {
		let waiting: Vec<&Worker<B::Job>> = self
			.workers
			.iter()
			.filter(|worker| worker.pending_upload.load(Ordering::SeqCst))
			.collect();

		if waiting.is_empty() {
			return 0;
		}

		self.backend.begin_upload();

		let mut uploads = 0;

		for worker in &waiting {
			if worker.status() != LoadStatus::Success {
				continue;
			}

			if let Some(job) = lock(&worker.job).as_mut()
				&& self.backend.upload(job)
			{
				uploads += 1;
			}
		}

		self.backend.end_upload();

		for worker in &waiting {
			let status = worker.status();

			if let Some(job) = lock(&worker.job).take() {
				self.backend.finish(job, status);
			}

			let was_pending = worker.pending_upload.swap(false, Ordering::SeqCst);

			if was_pending && worker.pending_upload.load(Ordering::SeqCst) {
				self.manager_signal.signal();
			}

			worker
				.status
				.store(LoadStatus::None as u8, Ordering::SeqCst);
			worker.busy.store(false, Ordering::SeqCst);
		}

		uploads
	}

	/// Hands the next queued job to an idle worker. Returns 1 if it did.
	fn check_for_items_to_load(&self) -> i32 {
		if lock(&self.queue).is_empty() {
			return 0;
		}

		let Some((index, worker)) = self
			.workers
			.iter()
			.enumerate()
			.find(|(_, worker)| !worker.busy.swap(true, Ordering::SeqCst))
		else {
			return 0;
		};

		let Some(job) = lock(&self.queue).pop_front() else {
			worker.busy.store(false, Ordering::SeqCst);
			return 0;
		};

		self.backend
			.log(LogLevel::Info, &format!("Found worker (id={index})"));

		*lock(&worker.job) = Some(job);
		worker.ready.signal();

		1
	}

	/// Destroys the resource at the front of the deletion queue if its time has come. Returns 1 if it
	/// was.
	fn check_for_items_to_delete(&self) -> i32 {
		if lock(&self.deletions).is_empty() {
			return 0;
		}

		self.backend.wait_for_uploads();

		let mut deletions = lock(&self.deletions);

		let Some(front) = deletions.front() else {
			return 0;
		};

		if !front.is_due(self.backend.frame()) {
			return 0;
		}

		let deletion = deletions.pop_front().expect("the front was just seen");

		drop(deletions);

		self.backend.destroy(deletion.resource);

		1
	}

	fn manager_loop(&self) {
		let mut should_sleep = false;
		let mut idle_since: Option<Instant> = None;

		while self.active.load(Ordering::SeqCst) {
			if should_sleep {
				self.manager_signal.wait();
				should_sleep = false;
			}

			if !self.active.load(Ordering::SeqCst) {
				break;
			}

			let uploads = self.check_for_uploadable_data();
			let deletes = self.check_for_items_to_delete();
			let loads = self.check_for_items_to_load();

			let operations = uploads + deletes + loads;

			if operations > 0 {
				self.manager_signal.discard(operations);
				idle_since = None;
				continue;
			}

			self.manager_signal.discard(1);

			let since = *idle_since.get_or_insert_with(Instant::now);

			let deletions_waiting = !lock(&self.deletions).is_empty();

			if deletions_waiting {
				idle_since = None;
			} else if since.elapsed() >= self.idle_sleep_after {
				should_sleep = true;
				self.backend
					.log(LogLevel::Info, "Sleeping asset manager...");
				continue;
			}

			thread::sleep(self.idle_poll);
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::atomic::AtomicU32;

	#[derive(Debug, Clone, PartialEq, Eq)]
	enum Event {
		Load(usize),
		BeginUpload,
		Upload(usize),
		EndUpload,
		Finish(usize, LoadStatus),
		WaitForUploads,
		Destroy(usize),
	}

	struct Probe {
		events: Mutex<Vec<Event>>,
		frame: AtomicU32,
		fail: Mutex<Vec<usize>>,
		load_delay: Duration,
		sleeping_logs: AtomicU32,
	}

	struct TestBackend(Arc<Probe>);

	impl Backend for TestBackend {
		type Job = usize;
		type Resource = usize;

		fn load(&self, job: &mut usize) -> LoadStatus {
			let job = *job;
			self.0.events.lock().unwrap().push(Event::Load(job));
			thread::sleep(self.0.load_delay);

			if self.0.fail.lock().unwrap().contains(&job) {
				LoadStatus::Error
			} else {
				LoadStatus::Success
			}
		}

		fn begin_upload(&self) {
			self.0.events.lock().unwrap().push(Event::BeginUpload);
		}

		fn upload(&self, job: &mut usize) -> bool {
			self.0.events.lock().unwrap().push(Event::Upload(*job));
			true
		}

		fn end_upload(&self) {
			self.0.events.lock().unwrap().push(Event::EndUpload);
		}

		fn finish(&self, job: usize, status: LoadStatus) {
			self.0
				.events
				.lock()
				.unwrap()
				.push(Event::Finish(job, status));
		}

		fn frame(&self) -> u32 {
			self.0.frame.load(Ordering::SeqCst)
		}

		fn wait_for_uploads(&self) {
			self.0.events.lock().unwrap().push(Event::WaitForUploads);
		}

		fn destroy(&self, resource: usize) {
			self.0.events.lock().unwrap().push(Event::Destroy(resource));
		}

		fn log(&self, _level: LogLevel, message: &str) {
			if message.contains("Sleeping") {
				self.0.sleeping_logs.fetch_add(1, Ordering::SeqCst);
			}
		}
	}

	fn probe(load_delay: Duration) -> Arc<Probe> {
		Arc::new(Probe {
			events: Mutex::new(Vec::new()),
			frame: AtomicU32::new(100),
			fail: Mutex::new(Vec::new()),
			load_delay,
			sleeping_logs: AtomicU32::new(0),
		})
	}

	fn scheduler(probe: &Arc<Probe>, workers: u32) -> Scheduler<TestBackend> {
		let mut config = SchedulerConfig::new(workers);
		config.idle_poll = Duration::from_millis(2);
		config.idle_sleep_after = Duration::from_millis(60);

		Scheduler::new(TestBackend(Arc::clone(probe)), config)
	}

	fn job(id: usize) -> usize {
		id
	}

	fn wait_until(mut condition: impl FnMut() -> bool) {
		let start = Instant::now();

		while !condition() {
			assert!(start.elapsed() < Duration::from_secs(5), "timed out");
			thread::sleep(Duration::from_millis(2));
		}
	}

	fn events(probe: &Arc<Probe>) -> Vec<Event> {
		probe.events.lock().unwrap().clone()
	}

	fn finished(probe: &Arc<Probe>) -> usize {
		events(probe)
			.iter()
			.filter(|event| matches!(event, Event::Finish(..)))
			.count()
	}

	#[test]
	fn a_job_is_loaded_then_uploaded_in_a_batch_then_finished() {
		let probe = probe(Duration::ZERO);
		let scheduler = scheduler(&probe, 2);

		scheduler.submit(job(1));
		wait_until(|| finished(&probe) == 1);

		assert_eq!(
			events(&probe),
			vec![
				Event::Load(1),
				Event::BeginUpload,
				Event::Upload(1),
				Event::EndUpload,
				Event::Finish(1, LoadStatus::Success),
			]
		);
	}

	#[test]
	fn a_job_that_fails_to_load_is_finished_without_being_uploaded() {
		let probe = probe(Duration::ZERO);
		probe.fail.lock().unwrap().push(7);
		let scheduler = scheduler(&probe, 1);

		scheduler.submit(job(7));
		wait_until(|| finished(&probe) == 1);

		let events = events(&probe);

		assert!(events.contains(&Event::Finish(7, LoadStatus::Error)));
		assert!(!events.iter().any(|event| matches!(event, Event::Upload(_))));
	}

	#[test]
	fn every_job_is_finished_once_whatever_the_number_of_workers() {
		for workers in [1, 2, 4] {
			let probe = probe(Duration::from_millis(1));
			let scheduler = scheduler(&probe, workers);

			for id in 1..=20 {
				scheduler.submit(job(id));
			}

			wait_until(|| finished(&probe) == 20);

			let events = events(&probe);

			for id in 1..=20 {
				let count = |wanted: &Event| events.iter().filter(|event| *event == wanted).count();

				assert_eq!(count(&Event::Load(id)), 1, "{workers} workers, job {id}");
				assert_eq!(count(&Event::Upload(id)), 1);
				assert_eq!(count(&Event::Finish(id, LoadStatus::Success)), 1);
			}

			assert_eq!(scheduler.queued_jobs(), 0);
		}
	}

	#[test]
	fn with_one_worker_jobs_load_in_the_order_they_were_submitted() {
		let probe = probe(Duration::ZERO);
		let scheduler = scheduler(&probe, 1);

		for id in 1..=8 {
			scheduler.submit(job(id));
		}

		wait_until(|| finished(&probe) == 8);

		let loads: Vec<usize> = events(&probe)
			.iter()
			.filter_map(|event| match event {
				Event::Load(id) => Some(*id),
				_ => None,
			})
			.collect();

		assert_eq!(loads, (1..=8).collect::<Vec<_>>());
	}

	#[test]
	fn jobs_that_load_together_are_uploaded_in_one_batch_each_after_its_begin() {
		let probe = probe(Duration::from_millis(2));
		let scheduler = scheduler(&probe, 4);

		for id in 1..=12 {
			scheduler.submit(job(id));
		}

		wait_until(|| finished(&probe) == 12);

		let events = events(&probe);
		let mut inside_batch = false;

		for event in &events {
			match event {
				Event::BeginUpload => {
					assert!(!inside_batch);
					inside_batch = true;
				}
				Event::EndUpload => {
					assert!(inside_batch);
					inside_batch = false;
				}
				Event::Upload(_) => assert!(inside_batch, "an upload outside a batch"),
				_ => {}
			}
		}

		assert!(!inside_batch);
		assert!(
			events
				.iter()
				.filter(|event| **event == Event::BeginUpload)
				.count() <= 12
		);
	}

	#[test]
	fn a_resource_waits_for_its_frame_and_is_then_destroyed_after_waiting_for_uploads() {
		let probe = probe(Duration::ZERO);
		let scheduler = scheduler(&probe, 1);

		scheduler.delete_resource(5, 4);

		thread::sleep(Duration::from_millis(30));
		assert!(!events(&probe).contains(&Event::Destroy(5)));
		assert!(events(&probe).contains(&Event::WaitForUploads));

		probe.frame.store(103, Ordering::SeqCst);
		thread::sleep(Duration::from_millis(30));
		assert!(!events(&probe).contains(&Event::Destroy(5)));

		probe.frame.store(104, Ordering::SeqCst);
		wait_until(|| events(&probe).contains(&Event::Destroy(5)));
		assert_eq!(scheduler.queued_deletions(), 0);
	}

	#[test]
	fn resources_are_destroyed_in_the_order_they_were_queued() {
		let probe = probe(Duration::ZERO);
		let scheduler = scheduler(&probe, 1);

		for id in 1..=4 {
			scheduler.delete_resource(id, 0);
		}

		wait_until(|| scheduler.queued_deletions() == 0);

		let destroyed: Vec<usize> = events(&probe)
			.iter()
			.filter_map(|event| match event {
				Event::Destroy(id) => Some(*id),
				_ => None,
			})
			.collect();

		assert_eq!(destroyed, vec![1, 2, 3, 4]);
	}

	#[test]
	fn flushing_destroys_everything_waiting_even_if_it_is_not_due() {
		let probe = probe(Duration::ZERO);
		let scheduler = scheduler(&probe, 1);

		scheduler.delete_resource(1, 1000);
		scheduler.delete_resource(2, 1000);
		scheduler.stop();
		scheduler.flush_deletions();

		let destroyed = events(&probe)
			.iter()
			.filter(|event| matches!(event, Event::Destroy(_)))
			.count();

		assert_eq!(destroyed, 2);
	}

	#[test]
	fn the_deletion_time_counts_a_wrapped_frame_counter() {
		let deletion = Deletion {
			resource: 1usize,
			min_frame: 10,
		};

		assert!(!deletion.is_due(9));
		assert!(deletion.is_due(10));
		assert!(deletion.is_due(11));

		let wrapped = Deletion {
			resource: 1usize,
			min_frame: 5,
		};

		assert!(!wrapped.is_due(u32::MAX - 50_000));
		assert!(!wrapped.is_due(u32::MAX - 5));
		assert!(!wrapped.is_due(4));
		assert!(wrapped.is_due(5));
		assert!(wrapped.is_due(6));
	}

	#[test]
	fn an_idle_manager_goes_to_sleep_and_is_woken_by_new_work() {
		let probe = probe(Duration::ZERO);
		let scheduler = scheduler(&probe, 1);

		wait_until(|| probe.sleeping_logs.load(Ordering::SeqCst) == 1);
		thread::sleep(Duration::from_millis(80));
		assert_eq!(probe.sleeping_logs.load(Ordering::SeqCst), 1);

		scheduler.submit(job(1));
		wait_until(|| finished(&probe) == 1);

		scheduler.delete_resource(3, 0);
		wait_until(|| events(&probe).contains(&Event::Destroy(3)));
	}

	#[test]
	fn stopping_ends_every_thread_even_while_asleep_or_busy() {
		let asleep = probe(Duration::ZERO);
		let scheduler_asleep = scheduler(&asleep, 3);

		wait_until(|| asleep.sleeping_logs.load(Ordering::SeqCst) == 1);
		scheduler_asleep.stop();

		let busy = probe(Duration::from_millis(30));
		let scheduler_busy = scheduler(&busy, 2);

		for id in 1..=10 {
			scheduler_busy.submit(job(id));
		}

		thread::sleep(Duration::from_millis(10));
		scheduler_busy.stop();
	}

	#[test]
	fn stopping_twice_does_nothing_the_second_time() {
		let probe = probe(Duration::ZERO);
		let scheduler = scheduler(&probe, 2);

		scheduler.stop();
		scheduler.stop();
	}

	#[test]
	fn more_workers_than_the_most_allowed_are_cut_back() {
		let probe = probe(Duration::ZERO);
		let scheduler = scheduler(&probe, 50);

		assert_eq!(scheduler.shared.workers.len(), MAX_WORKERS as usize);
	}
}
