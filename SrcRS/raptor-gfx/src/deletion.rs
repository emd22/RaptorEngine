use std::collections::VecDeque;
use std::sync::Mutex;

use raptor_gpu::{Allocator, Device};

type Task = Box<dyn FnOnce(&Device, &Allocator)>;

struct Pending {
	frame: u32,
	task: Task,
}

pub struct DeletionQueue {
	pending: Mutex<VecDeque<Pending>>,
}

// SAFETY: queued tasks only hold Vulkan resources, which are destroyed under the same external
// synchronisation rules as the rest of the backend; the queue itself is behind a mutex.
unsafe impl Send for DeletionQueue {}
// SAFETY: as above.
unsafe impl Sync for DeletionQueue {}

impl Default for DeletionQueue {
	fn default() -> Self {
		Self::new()
	}
}

impl DeletionQueue {
	pub fn new() -> Self {
		Self {
			pending: Mutex::new(VecDeque::new()),
		}
	}

	fn lock(&self) -> std::sync::MutexGuard<'_, VecDeque<Pending>> {
		self.pending
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn push(&self, frame: u32, task: impl FnOnce(&Device, &Allocator) + 'static) {
		self.lock().push_back(Pending {
			frame,
			task: Box::new(task),
		});
	}

	pub fn len(&self) -> usize {
		self.lock().len()
	}

	pub fn is_empty(&self) -> bool {
		self.lock().is_empty()
	}

	pub fn take_ready(&self, elapsed: u32) -> Vec<Task> {
		let mut pending = self.lock();
		let mut ready = Vec::new();

		while pending.front().is_some_and(|front| elapsed >= front.frame) {
			if let Some(item) = pending.pop_front() {
				ready.push(item.task);
			}
		}

		ready
	}

	pub fn process(&self, elapsed: u32, device: &Device, allocator: &Allocator) -> usize {
		let ready = self.take_ready(elapsed);
		let count = ready.len();

		for task in ready {
			task(device, allocator);
		}

		count
	}

	pub fn flush(&self, device: &Device, allocator: &Allocator) -> usize {
		let all: Vec<_> = self.lock().drain(..).collect();
		let count = all.len();

		for item in all {
			(item.task)(device, allocator);
		}

		count
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn entries_become_ready_once_their_frame_has_passed_in_order() {
		let queue = DeletionQueue::new();

		for frame in [1, 2, 5] {
			queue.push(frame, move |_, _| {});
		}

		assert_eq!(queue.len(), 3);
		assert_eq!(queue.take_ready(0).len(), 0);
		assert_eq!(queue.take_ready(2).len(), 2);
		assert_eq!(queue.len(), 1);
		assert_eq!(queue.take_ready(9).len(), 1);
		assert!(queue.is_empty());
	}
}
