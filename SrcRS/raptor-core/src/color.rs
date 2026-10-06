#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct Color(pub u32);

impl Color
{
	pub const NONE: Color = Color::from_rgba(0, 0, 0, 0);
	pub const TRANSPARENT: Color = Color::from_rgba(255, 255, 255, 0);
	pub const BLACK: Color = Color::from_rgba(0, 0, 0, 255);
	pub const WHITE: Color = Color::from_rgba(255, 255, 255, 255);
	pub const RED: Color = Color::from_rgba(255, 0, 0, 255);
	pub const GREEN: Color = Color::from_rgba(0, 255, 0, 255);
	pub const BLUE: Color = Color::from_rgba(0, 0, 255, 255);

	pub const fn from_rgba(r: u8, g: u8, b: u8, a: u8) -> Color
	{
		Color(u32::from_le_bytes([r, g, b, a]))
	}

	pub fn from_floats(rgba: [f32; 4]) -> Color
	{
		let byte = |value: f32| (value * 255.0).clamp(0.0, 255.0) as u8;

		Color::from_rgba(byte(rgba[0]), byte(rgba[1]), byte(rgba[2]), byte(rgba[3]))
	}

	pub const fn r(self) -> u8
	{
		self.0.to_le_bytes()[0]
	}

	pub const fn g(self) -> u8
	{
		self.0.to_le_bytes()[1]
	}

	pub const fn b(self) -> u8
	{
		self.0.to_le_bytes()[2]
	}

	pub const fn a(self) -> u8
	{
		self.0.to_le_bytes()[3]
	}

	pub fn rf(self) -> f32
	{
		f32::from(self.r()) / 255.0
	}

	pub fn gf(self) -> f32
	{
		f32::from(self.g()) / 255.0
	}

	pub fn bf(self) -> f32
	{
		f32::from(self.b()) / 255.0
	}

	pub fn af(self) -> f32
	{
		f32::from(self.a()) / 255.0
	}

	pub fn srgb_to_linear(value: f32) -> f32
	{
		if value <= 0.04045 {
			value / 12.92
		} else {
			((value + 0.055) / 1.055).powf(2.4)
		}
	}

	pub fn linear_rgb(self) -> [f32; 3]
	{
		[
			Color::srgb_to_linear(self.rf()),
			Color::srgb_to_linear(self.gf()),
			Color::srgb_to_linear(self.bf()),
		]
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn channels_round_trip()
	{
		let color = Color::from_rgba(1, 2, 3, 4);

		assert_eq!((color.r(), color.g(), color.b(), color.a()), (1, 2, 3, 4));
		assert_eq!(Color::RED.r(), 255);
		assert_eq!(Color::from_floats([1.0, 0.0, 0.0, 1.0]), Color::RED);
	}

	#[test]
	fn linear_conversion()
	{
		assert_eq!(Color::srgb_to_linear(0.0), 0.0);
		assert!((Color::srgb_to_linear(1.0) - 1.0).abs() < 1e-6);
	}
}
