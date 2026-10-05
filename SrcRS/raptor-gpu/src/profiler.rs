use ash::vk;

use crate::{Device, Level};

pub const MARKER_COUNT: usize = 8;

/// How long timings are averaged over before they are shown
pub const WINDOW_SECONDS: f64 = 0.25;

/// Turns the timestamps of one frame into the time each marker took since the one before it.
/// `results` holds a value and an availability flag for each marker, and `written` which markers
/// were recorded. Returns none if a recorded marker did not land, which makes the frame unusable.
pub fn stage_times(
	results: &[[u64; 2]; MARKER_COUNT],
	written: u32,
	valid_mask: u64,
	ms_per_tick: f64,
) -> Option<[f64; MARKER_COUNT]>
{
	let mut stage_ms = [0.0; MARKER_COUNT];
	let mut previous: Option<usize> = None;

	for marker in 0..MARKER_COUNT {
		if written & (1 << marker) == 0 {
			continue;
		}

		if results[marker][1] == 0 {
			return None;
		}

		if let Some(previous) = previous {
			let ticks = results[marker][0].wrapping_sub(results[previous][0]) & valid_mask;

			stage_ms[marker] = ticks as f64 * ms_per_tick;
		}

		previous = Some(marker);
	}

	Some(stage_ms)
}

/// Averages frame timings over fixed windows of time.
#[derive(Default)]
pub struct TimingWindow
{
	sums: [f64; MARKER_COUNT],
	time: f64,
	frames: u32,
	averages: [f64; MARKER_COUNT],
}

impl TimingWindow
{
	pub fn add_frame(&mut self, stage_ms: &[f64; MARKER_COUNT], delta_seconds: f64)
	{
		for (sum, ms) in self.sums.iter_mut().zip(stage_ms) {
			*sum += ms;
		}

		self.frames += 1;
		self.time += delta_seconds;

		if self.time >= WINDOW_SECONDS && self.frames > 0 {
			for (average, sum) in self.averages.iter_mut().zip(&mut self.sums) {
				*average = *sum / f64::from(self.frames);
				*sum = 0.0;
			}

			self.frames = 0;
			self.time = 0.0;
		}
	}

	pub fn average(&self, marker: usize) -> f64
	{
		self.averages.get(marker).copied().unwrap_or(0.0)
	}

	pub fn total(&self) -> f64
	{
		self.averages.iter().sum()
	}
}

/// Times the stages of the GPU frame with timestamp queries, one query pool for each frame in
/// flight.
pub struct GpuProfiler
{
	pools: Vec<vk::QueryPool>,
	written: Vec<u32>,
	current_frame: usize,
	ms_per_tick: f64,
	valid_mask: u64,
	window: TimingWindow,
}

impl GpuProfiler
{
	/// Makes the profiler, or returns none if the GPU can not take timestamps.
	pub fn create(device: &Device, graphics_family: u32, frames_in_flight: u32)
	-> Option<Box<Self>>
	{
		let properties = device.physical_properties();
		let valid_bits = device.timestamp_valid_bits(graphics_family);
		let period = properties.limits.timestamp_period;

		if properties.limits.timestamp_compute_and_graphics == vk::FALSE
			|| valid_bits == 0
			|| period <= 0.0
		{
			device.log().log(
				Level::Warning,
				&format!(
					"GPU timestamps are not supported (compute and graphics={}, valid bits={}, period={})",
					properties.limits.timestamp_compute_and_graphics, valid_bits, period
				),
			);
			return None;
		}

		let info = vk::QueryPoolCreateInfo::default()
			.query_type(vk::QueryType::TIMESTAMP)
			.query_count(MARKER_COUNT as u32);

		let mut profiler = Box::new(Self {
			pools: Vec::new(),
			written: vec![0; frames_in_flight as usize],
			current_frame: 0,
			// The period is nanoseconds per tick
			ms_per_tick: f64::from(period) * 1e-6,
			valid_mask: if valid_bits >= 64 {
				u64::MAX
			} else {
				(1 << valid_bits) - 1
			},
			window: TimingWindow::default(),
		});

		for _ in 0..frames_in_flight {
			// SAFETY: the device is alive.
			match unsafe { device.raw().create_query_pool(&info, None) } {
				Ok(pool) => profiler.pools.push(pool),
				Err(_) => {
					device.log().log(
						Level::Warning,
						"Could not create a timestamp query pool, GPU timings are off",
					);

					// SAFETY: the pools were just made and nothing has used them.
					unsafe { profiler.destroy(device) };

					return None;
				}
			}
		}

		device.log().log(
			Level::Info,
			&format!("GPU timestamps are on ({valid_bits} valid bits, {period} ns per tick)"),
		);

		Some(profiler)
	}

	/// # Safety
	///
	/// The device must be the one the profiler was made with, and nothing may be using the pools.
	pub unsafe fn destroy(self, device: &Device)
	{
		for pool in self.pools {
			// SAFETY: guaranteed by the caller.
			unsafe { device.raw().destroy_query_pool(pool, None) };
		}
	}

	/// Reads back what the frame slot's last use timed and folds it into the averages. Call once
	/// the slot's fence has been waited on, before the slot is recorded again.
	pub fn read_results(&mut self, device: &Device, frame_index: usize, delta_seconds: f64)
	{
		let Some(written) = self
			.written
			.get(frame_index)
			.copied()
			.filter(|written| *written != 0)
		else {
			return;
		};

		let mut results = [[0u64; 2]; MARKER_COUNT];

		// SAFETY: the pool belongs to the device, and the results are laid out as a value and an
		// availability flag for each query.
		let status = unsafe {
			device.raw().get_query_pool_results(
				self.pools[frame_index],
				0,
				&mut results,
				vk::QueryResultFlags::TYPE_64 | vk::QueryResultFlags::WITH_AVAILABILITY,
			)
		};

		// Not ready, or nothing in it: the frame was never submitted
		if !matches!(status, Ok(()) | Err(vk::Result::NOT_READY)) {
			return;
		}

		if let Some(stage_ms) = stage_times(&results, written, self.valid_mask, self.ms_per_tick) {
			self.window.add_frame(&stage_ms, delta_seconds);
		}
	}

	/// Resets the frame slot's queries and marks the start of the frame.
	///
	/// # Safety
	///
	/// `cmd` must be recording outside a render pass, and the slot's queries must not be in use.
	pub unsafe fn begin_frame(
		&mut self,
		device: &Device,
		cmd: vk::CommandBuffer,
		frame_index: usize,
	)
	{
		self.current_frame = frame_index;
		self.written[frame_index] = 0;

		// SAFETY: guaranteed by the caller.
		unsafe {
			device
				.raw()
				.cmd_reset_query_pool(cmd, self.pools[frame_index], 0, MARKER_COUNT as u32)
		};

		// SAFETY: guaranteed by the caller.
		unsafe { self.mark(device, cmd, 0) };
	}

	/// Writes a timestamp for the end of a stage into the frame being recorded.
	///
	/// # Safety
	///
	/// `cmd` must be recording, and `begin_frame` must have been called for the frame.
	pub unsafe fn mark(&mut self, device: &Device, cmd: vk::CommandBuffer, marker: usize)
	{
		if marker >= MARKER_COUNT {
			return;
		}

		// SAFETY: guaranteed by the caller.
		unsafe {
			device.raw().cmd_write_timestamp(
				cmd,
				vk::PipelineStageFlags::BOTTOM_OF_PIPE,
				self.pools[self.current_frame],
				marker as u32,
			)
		};

		self.written[self.current_frame] |= 1 << marker;
	}

	pub fn average_ms(&self, marker: usize) -> f64
	{
		self.window.average(marker)
	}

	pub fn total_ms(&self) -> f64
	{
		self.window.total()
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn landed(values: &[(usize, u64)]) -> [[u64; 2]; MARKER_COUNT]
	{
		let mut results = [[0u64; 2]; MARKER_COUNT];

		for (marker, value) in values {
			results[*marker] = [*value, 1];
		}

		results
	}

	#[test]
	fn each_stage_is_timed_from_the_marker_before_it()
	{
		let results = landed(&[(0, 100), (2, 400), (5, 1000)]);

		let times = stage_times(&results, 0b100101, u64::MAX, 0.5).unwrap();

		assert_eq!(times[0], 0.0);
		assert_eq!(times[2], 150.0);
		assert_eq!(times[5], 300.0);
		assert_eq!(times[1], 0.0);
	}

	#[test]
	fn a_frame_with_a_marker_that_did_not_land_is_dropped()
	{
		let mut results = landed(&[(0, 100), (2, 400)]);
		results[2][1] = 0;

		assert!(stage_times(&results, 0b101, u64::MAX, 1.0).is_none());
	}

	#[test]
	fn the_timestamp_counter_wrapping_is_handled_by_the_valid_mask()
	{
		let mask = (1u64 << 32) - 1;
		let results = landed(&[(0, mask - 9), (1, 5)]);

		let times = stage_times(&results, 0b11, mask, 1.0).unwrap();

		assert_eq!(times[1], 15.0);
	}

	#[test]
	fn the_window_averages_after_a_quarter_of_a_second()
	{
		let mut window = TimingWindow::default();
		let mut frame = [0.0; MARKER_COUNT];
		frame[3] = 2.0;

		window.add_frame(&frame, 0.1);
		window.add_frame(&frame, 0.1);
		assert_eq!(window.average(3), 0.0);

		frame[3] = 5.0;
		window.add_frame(&frame, 0.1);

		assert_eq!(window.average(3), 3.0);
		assert_eq!(window.total(), 3.0);

		window.add_frame(&frame, 0.3);
		assert_eq!(window.average(3), 5.0);
	}
}
