#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ExposureSettings {
	pub aperture: f32,
	pub shutter_time: f32,
	pub iso: f32,
	pub compensation: f32,
}

impl ExposureSettings {
	pub const MIN_VALUE: f32 = 1e-4;
	pub const METER_CALIBRATION: f32 = 1.2;

	pub fn ev100(&self) -> f32 {
		let aperture = self.aperture.max(Self::MIN_VALUE);
		let shutter = self.shutter_time.max(Self::MIN_VALUE);
		let iso = self.iso.max(Self::MIN_VALUE);

		(aperture * aperture / shutter * (100.0 / iso)).log2()
	}

	pub fn exposure(&self) -> f32 {
		self.compensation.exp2() / (Self::METER_CALIBRATION * self.ev100().exp2())
	}
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

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn default_exposure_matches_the_sunny_16_rule() {
		let settings = ExposureSettings::default();

		assert!((settings.ev100() - 14.642).abs() < 1e-2);
		assert!(settings.exposure() > 0.0);
	}

	#[test]
	fn compensation_doubles_the_exposure_per_stop() {
		let base = ExposureSettings::default();
		let bright = ExposureSettings {
			compensation: 1.0,
			..base
		};

		assert!((bright.exposure() / base.exposure() - 2.0).abs() < 1e-5);
	}
}
