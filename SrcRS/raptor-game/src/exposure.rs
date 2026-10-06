const MIN_VALUE: f32 = 1e-4;
const METER_CALIBRATION: f32 = 1.2;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ExposureSettings {
	pub aperture: f32,
	pub shutter_time: f32,
	pub iso: f32,
	pub compensation: f32,
}

impl Default for ExposureSettings {
	fn default() -> Self {
		Self {
			aperture: 16.0,
			shutter_time: 0.01,
			iso: 100.0,
			compensation: 0.0,
		}
	}
}

impl ExposureSettings {
	pub fn ev100(&self) -> f32 {
		let aperture = self.aperture.max(MIN_VALUE);
		let shutter = self.shutter_time.max(MIN_VALUE);
		let iso = self.iso.max(MIN_VALUE);

		((aperture * aperture / shutter) * (100.0 / iso)).log2()
	}

	pub fn exposure(&self) -> f32 {
		self.compensation.exp2() / (METER_CALIBRATION * self.ev100().exp2())
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_default_camera_exposes_for_sunny_sixteen() {
		let settings = ExposureSettings::default();

		assert!((settings.ev100() - 14.644).abs() < 0.01);
		assert!((settings.exposure() - 1.0 / (1.2 * 25600.0)).abs() < 1e-6);
	}

	#[test]
	fn compensation_doubles_per_stop() {
		let base = ExposureSettings::default().exposure();
		let bright = ExposureSettings {
			compensation: 1.0,
			..ExposureSettings::default()
		}
		.exposure();

		assert!((bright / base - 2.0).abs() < 1e-5);
	}
}
