pub const WINDOW_SECONDS: f64 = 0.25;

#[derive(Default)]
pub struct FpsMeter {
	window_time: f64,
	window_start_frame: u64,
	pub fps: f64,
	pub frame_time_ms: f64,
}

impl FpsMeter {
	pub fn tick(&mut self, delta_time: f64, elapsed_frames: u64) {
		self.window_time += delta_time;

		if self.window_time < WINDOW_SECONDS {
			return;
		}

		let frames = elapsed_frames.saturating_sub(self.window_start_frame);

		if frames > 0 {
			self.frame_time_ms = (self.window_time / frames as f64) * 1000.0;
			self.fps = frames as f64 / self.window_time;
		}

		self.window_time = 0.0;
		self.window_start_frame = elapsed_frames;
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_rate_is_averaged_over_finished_frames_not_ticks() {
		let mut meter = FpsMeter::default();

		meter.tick(0.1, 3);
		assert_eq!(meter.fps, 0.0);

		meter.tick(0.2, 6);
		assert!((meter.fps - 20.0).abs() < 1e-9);
		assert!((meter.frame_time_ms - 50.0).abs() < 1e-9);
	}

	#[test]
	fn a_window_without_finished_frames_keeps_the_last_reading() {
		let mut meter = FpsMeter::default();

		meter.tick(0.3, 6);
		let fps = meter.fps;

		meter.tick(0.3, 6);
		assert_eq!(meter.fps, fps);
	}
}
