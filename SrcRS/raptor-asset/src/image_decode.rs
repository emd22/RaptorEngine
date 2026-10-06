use std::path::Path;

use image::{ColorType, DynamicImage, ImageEncoder};

#[derive(Debug, PartialEq, Eq)]
pub enum ImageError {
	Unreadable(String),
	Unsupported(String),
}

impl std::fmt::Display for ImageError {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		match self {
			Self::Unreadable(message) | Self::Unsupported(message) => f.write_str(message),
		}
	}
}

/// 8-bit pixels with `channels` values each
#[derive(Debug, PartialEq, Eq)]
pub struct Pixels {
	pub width: u32,
	pub height: u32,
	pub channels: u32,
	pub data: Vec<u8>,
}

/// The samples of an image, as 8 or 16 bits to a channel
enum Samples {
	Eight(Vec<u8>),
	Sixteen(Vec<u16>),
}

/// An image as it was in its file
struct Native {
	width: u32,
	height: u32,
	channels: u32,
	samples: Samples,
}

fn native_of(image: DynamicImage) -> Result<Native, ImageError> {
	let (width, height) = (image.width(), image.height());

	let (channels, samples) = match image {
		DynamicImage::ImageLuma8(image) => (1, Samples::Eight(image.into_raw())),
		DynamicImage::ImageLumaA8(image) => (2, Samples::Eight(image.into_raw())),
		DynamicImage::ImageRgb8(image) => (3, Samples::Eight(image.into_raw())),
		DynamicImage::ImageRgba8(image) => (4, Samples::Eight(image.into_raw())),
		DynamicImage::ImageLuma16(image) => (1, Samples::Sixteen(image.into_raw())),
		DynamicImage::ImageLumaA16(image) => (2, Samples::Sixteen(image.into_raw())),
		DynamicImage::ImageRgb16(image) => (3, Samples::Sixteen(image.into_raw())),
		DynamicImage::ImageRgba16(image) => (4, Samples::Sixteen(image.into_raw())),
		other => {
			return Err(ImageError::Unsupported(format!(
				"images with {:?} pixels are not supported",
				other.color()
			)));
		}
	};

	Ok(Native {
		width,
		height,
		channels,
		samples,
	})
}

/// The brightness of a pixel, as stb computes it
fn luma8(red: u8, green: u8, blue: u8) -> u8 {
	((u32::from(red) * 77 + u32::from(green) * 150 + 29 * u32::from(blue)) >> 8) as u8
}

fn luma16(red: u16, green: u16, blue: u16) -> u16 {
	((u32::from(red) * 77 + u32::from(green) * 150 + 29 * u32::from(blue)) >> 8) as u16
}

/// Changes the number of channels of pixels the way stb_image does: a missing alpha is opaque, a gray
/// pixel is repeated across red, green and blue, and color is turned into gray by `luma`.
fn convert_channels<T: Copy>(
	data: &[T],
	channels: u32,
	wanted: u32,
	opaque: T,
	luma: impl Fn(T, T, T) -> T,
) -> Vec<T> {
	if channels == wanted {
		return data.to_vec();
	}

	let mut out = Vec::with_capacity(data.len() / channels as usize * wanted as usize);

	for pixel in data.chunks_exact(channels as usize) {
		match (channels, wanted) {
			(1, 2) => out.extend([pixel[0], opaque]),
			(1, 3) => out.extend([pixel[0]; 3]),
			(1, 4) => out.extend([pixel[0], pixel[0], pixel[0], opaque]),
			(2, 1) => out.push(pixel[0]),
			(2, 3) => out.extend([pixel[0]; 3]),
			(2, 4) => out.extend([pixel[0], pixel[0], pixel[0], pixel[1]]),
			(3, 1) => out.push(luma(pixel[0], pixel[1], pixel[2])),
			(3, 2) => out.extend([luma(pixel[0], pixel[1], pixel[2]), opaque]),
			(3, 4) => out.extend([pixel[0], pixel[1], pixel[2], opaque]),
			(4, 1) => out.push(luma(pixel[0], pixel[1], pixel[2])),
			(4, 2) => out.extend([luma(pixel[0], pixel[1], pixel[2]), pixel[3]]),
			(4, 3) => out.extend([pixel[0], pixel[1], pixel[2]]),
			_ => {}
		}
	}

	out
}

/// Pixels of `wanted` channels at 8 bits each. A 16-bit image is converted to the channels first and
/// only then cut down to its top bytes, as stb does.
fn to_eight_bit(native: &Native, wanted: u32) -> Vec<u8> {
	match &native.samples {
		Samples::Eight(data) => convert_channels(data, native.channels, wanted, 255, luma8),
		Samples::Sixteen(data) => convert_channels(data, native.channels, wanted, u16::MAX, luma16)
			.into_iter()
			.map(|sample| (sample >> 8) as u8)
			.collect(),
	}
}

/// How big an image is, without decoding it
pub fn image_size(data: &[u8]) -> Option<(u32, u32)> {
	image::ImageReader::new(std::io::Cursor::new(data))
		.with_guessed_format()
		.ok()?
		.into_dimensions()
		.ok()
}

/// Decodes an image of any format the engine reads (PNG, JPEG, BMP, TGA, GIF) to `channels` 8-bit
/// channels, whatever it has in the file.
pub fn decode_image(data: &[u8], channels: u32) -> Result<Pixels, ImageError> {
	if !(1..=4).contains(&channels) {
		return Err(ImageError::Unsupported(format!(
			"{channels} channels are not a pixel size"
		)));
	}

	let image = image::ImageReader::new(std::io::Cursor::new(data))
		.with_guessed_format()
		.map_err(|error| ImageError::Unreadable(error.to_string()))?
		.decode()
		.map_err(|error| ImageError::Unreadable(error.to_string()))?;

	let native = native_of(image)?;

	Ok(Pixels {
		width: native.width,
		height: native.height,
		channels,
		data: to_eight_bit(&native, channels),
	})
}

pub fn decode_file(path: &Path, channels: u32) -> Result<Pixels, ImageError> {
	let data = std::fs::read(path).map_err(|error| {
		ImageError::Unreadable(format!("Could not read '{}': {error}", path.display()))
	})?;

	decode_image(&data, channels)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SaveFormat {
	Jpeg,
	Png,
}

const JPEG_QUALITY: u8 = 90;

/// Writes RGBA pixels as a file. A JPEG leaves out the alpha.
pub fn save_rgba(
	path: &Path,
	format: SaveFormat,
	rgba: &[u8],
	width: u32,
	height: u32,
	flip_y: bool,
) -> Result<(), ImageError> {
	let expected = width as usize * height as usize * 4;

	if rgba.len() < expected {
		return Err(ImageError::Unsupported(format!(
			"{} bytes are not enough for a {width} by {height} image",
			rgba.len()
		)));
	}

	let mut pixels = rgba[..expected].to_vec();

	if flip_y {
		let row = width as usize * 4;

		for y in 0..height as usize / 2 {
			let (top, bottom) = pixels.split_at_mut((height as usize - 1 - y) * row);

			top[y * row..(y + 1) * row].swap_with_slice(&mut bottom[..row]);
		}
	}

	let file = std::fs::File::create(path).map_err(|error| {
		ImageError::Unreadable(format!("Could not create '{}': {error}", path.display()))
	})?;

	let writer = std::io::BufWriter::new(file);

	let result =
		match format {
			SaveFormat::Png => image::codecs::png::PngEncoder::new(writer).write_image(
				&pixels,
				width,
				height,
				ColorType::Rgba8.into(),
			),
			SaveFormat::Jpeg => {
				let rgb: Vec<u8> = pixels
					.as_chunks::<4>()
					.0
					.iter()
					.flat_map(|pixel| [pixel[0], pixel[1], pixel[2]])
					.collect();

				image::codecs::jpeg::JpegEncoder::new_with_quality(writer, JPEG_QUALITY)
					.write_image(&rgb, width, height, ColorType::Rgb8.into())
			}
		};

	result.map_err(|error| ImageError::Unreadable(error.to_string()))
}

#[cfg(test)]
mod tests {
	use super::*;

	fn png_of(color: ColorType, width: u32, height: u32, data: &[u8]) -> Vec<u8> {
		let mut out = Vec::new();

		image::codecs::png::PngEncoder::new(&mut out)
			.write_image(data, width, height, color.into())
			.unwrap();

		out
	}

	#[test]
	fn an_rgba_png_decodes_to_the_same_pixels() {
		let pixels: Vec<u8> = (0..16).map(|value| value * 13).collect();
		let png = png_of(ColorType::Rgba8, 2, 2, &pixels);

		let decoded = decode_image(&png, 4).unwrap();

		assert_eq!((decoded.width, decoded.height, decoded.channels), (2, 2, 4));
		assert_eq!(decoded.data, pixels);
	}

	#[test]
	fn rgb_gets_an_opaque_alpha() {
		let png = png_of(ColorType::Rgb8, 2, 1, &[1, 2, 3, 4, 5, 6]);

		assert_eq!(
			decode_image(&png, 4).unwrap().data,
			vec![1, 2, 3, 255, 4, 5, 6, 255]
		);
	}

	#[test]
	fn gray_is_repeated_across_the_color_channels() {
		let png = png_of(ColorType::L8, 2, 1, &[10, 200]);

		assert_eq!(
			decode_image(&png, 4).unwrap().data,
			vec![10, 10, 10, 255, 200, 200, 200, 255]
		);
		assert_eq!(
			decode_image(&png, 3).unwrap().data,
			vec![10, 10, 10, 200, 200, 200]
		);
		assert_eq!(decode_image(&png, 2).unwrap().data, vec![10, 255, 200, 255]);
		assert_eq!(decode_image(&png, 1).unwrap().data, vec![10, 200]);
	}

	#[test]
	fn gray_with_alpha_keeps_its_alpha() {
		let png = png_of(ColorType::La8, 1, 1, &[50, 60]);

		assert_eq!(decode_image(&png, 4).unwrap().data, vec![50, 50, 50, 60]);
		assert_eq!(decode_image(&png, 2).unwrap().data, vec![50, 60]);
	}

	#[test]
	fn color_is_turned_into_gray_with_stbs_weights() {
		let png = png_of(ColorType::Rgba8, 2, 1, &[255, 0, 0, 9, 0, 255, 0, 9]);

		assert_eq!(decode_image(&png, 1).unwrap().data, vec![76, 149]);
		assert_eq!(decode_image(&png, 2).unwrap().data, vec![76, 9, 149, 9]);
		assert_eq!(
			decode_image(&png, 3).unwrap().data,
			vec![255, 0, 0, 0, 255, 0]
		);
	}

	#[test]
	fn sixteen_bit_pixels_keep_their_top_byte() {
		let samples: Vec<u8> = [0x1234u16, 0xFFFF, 0x00FF, 0x8000]
			.iter()
			.flat_map(|value| value.to_ne_bytes())
			.collect();

		let png = png_of(ColorType::Rgba16, 1, 1, &samples);

		assert_eq!(
			decode_image(&png, 4).unwrap().data,
			vec![0x12, 0xFF, 0x00, 0x80]
		);
	}

	#[test]
	fn the_size_can_be_read_without_decoding() {
		let png = png_of(ColorType::Rgb8, 3, 2, &[0; 18]);

		assert_eq!(image_size(&png), Some((3, 2)));
		assert_eq!(image_size(b"junk"), None);
	}

	#[test]
	fn junk_and_bad_channel_counts_are_errors() {
		assert!(matches!(
			decode_image(b"junk", 4),
			Err(ImageError::Unreadable(_))
		));

		let png = png_of(ColorType::Rgb8, 1, 1, &[0; 3]);

		assert!(matches!(
			decode_image(&png, 0),
			Err(ImageError::Unsupported(_))
		));
		assert!(matches!(
			decode_image(&png, 5),
			Err(ImageError::Unsupported(_))
		));
	}

	#[test]
	fn a_saved_png_reads_back_the_same_and_can_be_flipped() {
		let directory =
			std::env::temp_dir().join(format!("raptor_image_save_{}", std::process::id()));
		std::fs::create_dir_all(&directory).unwrap();

		let pixels: Vec<u8> = (0..2 * 3 * 4).map(|value| value as u8 * 9).collect();

		let plain = directory.join("plain.png");
		let flipped = directory.join("flipped.png");

		save_rgba(&plain, SaveFormat::Png, &pixels, 2, 3, false).unwrap();
		save_rgba(&flipped, SaveFormat::Png, &pixels, 2, 3, true).unwrap();

		assert_eq!(decode_file(&plain, 4).unwrap().data, pixels);

		let flipped = decode_file(&flipped, 4).unwrap().data;

		assert_eq!(&flipped[..8], &pixels[16..24]);
		assert_eq!(&flipped[8..16], &pixels[8..16]);
		assert_eq!(&flipped[16..], &pixels[..8]);

		std::fs::remove_dir_all(&directory).unwrap();
	}

	#[test]
	fn a_saved_jpeg_reads_back_close_to_the_original() {
		let directory =
			std::env::temp_dir().join(format!("raptor_image_jpeg_{}", std::process::id()));
		std::fs::create_dir_all(&directory).unwrap();

		let pixels: Vec<u8> = (0..16 * 16)
			.flat_map(|index| [(index % 16) as u8 * 16, (index / 16) as u8 * 16, 128, 255])
			.collect();

		let path = directory.join("gradient.jpg");

		save_rgba(&path, SaveFormat::Jpeg, &pixels, 16, 16, false).unwrap();

		let decoded = decode_file(&path, 4).unwrap();

		assert_eq!((decoded.width, decoded.height), (16, 16));

		let worst = decoded
			.data
			.iter()
			.zip(&pixels)
			.map(|(a, b)| a.abs_diff(*b))
			.max()
			.unwrap();

		assert!(worst < 40, "{worst}");

		std::fs::remove_dir_all(&directory).unwrap();
	}

	#[test]
	fn saving_too_few_pixels_is_an_error() {
		let path = std::env::temp_dir().join("raptor_never_written.png");

		assert!(save_rgba(&path, SaveFormat::Png, &[0; 4], 4, 4, false).is_err());
	}
}
