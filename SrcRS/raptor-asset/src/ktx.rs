use std::io::Read;

use ktx2::SupercompressionScheme;

/// The pixel formats a KTX image can have, numbered as the engine's `eImageFormat`
pub const FORMAT_BGRA8_UNORM: u16 = 1;
pub const FORMAT_RGBA8_SRGB: u16 = 3;
pub const FORMAT_RGBA8_UNORM: u16 = 4;
pub const FORMAT_R8_UNORM: u16 = 16;

const GL_RGBA8: u32 = 0x8058;
const GL_SRGB8_ALPHA8: u32 = 0x8C43;

const KTX1_IDENTIFIER: [u8; 12] = [
	0xAB, b'K', b'T', b'X', b' ', b'1', b'1', 0xBB, b'\r', b'\n', 0x1A, b'\n',
];
const KTX2_IDENTIFIER: [u8; 12] = [
	0xAB, b'K', b'T', b'X', b' ', b'2', b'0', 0xBB, b'\r', b'\n', 0x1A, b'\n',
];

#[derive(Debug, PartialEq, Eq)]
pub enum KtxError {
	/// The data is not a KTX file, or is cut short or damaged
	Invalid(String),
	/// The texture is Basis compressed and would have to be transcoded
	NeedsTranscoding,
	UnsupportedFormat,
	/// Anything but a texture with one layer, one face and no depth
	NotPlain2d,
}

impl std::fmt::Display for KtxError {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		match self {
			Self::Invalid(message) => f.write_str(message),
			Self::NeedsTranscoding => {
				f.write_str("KTX: GPU-compressed (Basis) textures are not supported")
			}
			Self::UnsupportedFormat => f.write_str("KTX: unsupported pixel format"),
			Self::NotPlain2d => f.write_str("KTX: only plain 2D textures are supported"),
		}
	}
}

/// The pixels of a KTX texture, a mip level at a time, from the largest
#[derive(Debug, PartialEq, Eq)]
pub struct KtxImage {
	pub format: u16,
	pub width: u32,
	pub height: u32,
	pub levels: Vec<Vec<u8>>,
}

fn bytes_per_pixel(format: u16) -> usize {
	if format == FORMAT_R8_UNORM { 1 } else { 4 }
}

fn level_size(format: u16, width: u32, height: u32, level: u32) -> usize {
	let width = (width >> level).max(1) as usize;
	let height = (height >> level).max(1) as usize;

	width * height * bytes_per_pixel(format)
}

fn format_from_vk(vk_format: u32) -> Option<u16> {
	match vk_format {
		44 => Some(FORMAT_BGRA8_UNORM),
		37 => Some(FORMAT_RGBA8_UNORM),
		43 => Some(FORMAT_RGBA8_SRGB),
		9 => Some(FORMAT_R8_UNORM),
		_ => None,
	}
}

fn inflate(scheme: SupercompressionScheme, data: &[u8], size: usize) -> Result<Vec<u8>, KtxError> {
	let out = match scheme {
		SupercompressionScheme::ZLIB => miniz_oxide::inflate::decompress_to_vec_zlib(data)
			.map_err(|error| {
				KtxError::Invalid(format!("KTX: could not inflate a level: {error:?}"))
			})?,
		SupercompressionScheme::Zstandard => {
			let mut decoder = ruzstd::StreamingDecoder::new(data).map_err(|error| {
				KtxError::Invalid(format!("KTX: could not read a zstd level: {error}"))
			})?;
			let mut out = Vec::with_capacity(size);

			decoder.read_to_end(&mut out).map_err(|error| {
				KtxError::Invalid(format!("KTX: could not decompress a level: {error}"))
			})?;

			out
		}
		_ => return Err(KtxError::NeedsTranscoding),
	};

	Ok(out)
}

fn read_ktx2(data: &[u8]) -> Result<KtxImage, KtxError> {
	let reader =
		ktx2::Reader::new(data).map_err(|error| KtxError::Invalid(format!("{error:?}")))?;
	let header = reader.header();

	let scheme = header.supercompression_scheme;

	if scheme == Some(SupercompressionScheme::BasisLZ) {
		return Err(KtxError::NeedsTranscoding);
	}

	let Some(vk_format) = header.format else {
		return Err(KtxError::NeedsTranscoding);
	};

	let Some(format) = format_from_vk(vk_format.value()) else {
		return Err(KtxError::UnsupportedFormat);
	};

	if header.pixel_height == 0
		|| header.pixel_depth != 0
		|| header.layer_count > 1
		|| header.face_count != 1
	{
		return Err(KtxError::NotPlain2d);
	}

	let mut levels = Vec::new();

	for (index, level) in reader.levels().enumerate() {
		let expected = level_size(
			format,
			header.pixel_width,
			header.pixel_height,
			index as u32,
		);

		let bytes = match scheme {
			None => level.data.to_vec(),
			Some(scheme) => inflate(scheme, level.data, expected)?,
		};

		if bytes.len() != expected {
			return Err(KtxError::Invalid(format!(
				"KTX: level {index} has {} bytes where {expected} were expected",
				bytes.len()
			)));
		}

		levels.push(bytes);
	}

	if levels.is_empty() {
		return Err(KtxError::Invalid(
			"KTX: the texture has no levels".to_owned(),
		));
	}

	Ok(KtxImage {
		format,
		width: header.pixel_width,
		height: header.pixel_height,
		levels,
	})
}

fn read_ktx1(data: &[u8]) -> Result<KtxImage, KtxError> {
	const HEADER_SIZE: usize = 64;

	if data.len() < HEADER_SIZE {
		return Err(KtxError::Invalid(
			"KTX: the file is shorter than its header".to_owned(),
		));
	}

	let endianness = u32::from_le_bytes(data[12..16].try_into().expect("four bytes"));

	let swapped = match endianness {
		0x0403_0201 => false,
		0x0102_0304 => true,
		_ => {
			return Err(KtxError::Invalid(
				"KTX: the endianness field is not valid".to_owned(),
			));
		}
	};

	let word = |offset: usize| {
		let value = u32::from_le_bytes(data[offset..offset + 4].try_into().expect("four bytes"));

		if swapped { value.swap_bytes() } else { value }
	};

	let internal_format = word(28);
	let (width, height, depth) = (word(36), word(40), word(44));
	let (array_elements, faces, mip_levels, key_value_bytes) =
		(word(48), word(52), word(56), word(60));

	let format = match internal_format {
		GL_RGBA8 => FORMAT_RGBA8_UNORM,
		GL_SRGB8_ALPHA8 => FORMAT_RGBA8_SRGB,
		_ => return Err(KtxError::UnsupportedFormat),
	};

	if height == 0 || depth != 0 || array_elements > 1 || faces != 1 {
		return Err(KtxError::NotPlain2d);
	}

	let mut offset = HEADER_SIZE + key_value_bytes as usize;
	let mut levels = Vec::new();

	for level in 0..mip_levels.max(1) {
		let size_bytes = data
			.get(offset..offset + 4)
			.ok_or_else(|| KtxError::Invalid("KTX: a level is cut short".to_owned()))?;

		let image_size = {
			let value = u32::from_le_bytes(size_bytes.try_into().expect("four bytes"));

			if swapped { value.swap_bytes() } else { value }
		} as usize;

		offset += 4;

		let pixels = data
			.get(offset..offset + image_size)
			.ok_or_else(|| KtxError::Invalid("KTX: a level is cut short".to_owned()))?;

		if image_size != level_size(format, width, height, level) {
			return Err(KtxError::Invalid(format!(
				"KTX: level {level} has an unexpected size"
			)));
		}

		levels.push(pixels.to_vec());

		offset += image_size.next_multiple_of(4);
	}

	Ok(KtxImage {
		format,
		width,
		height,
		levels,
	})
}

/// Whether the bytes start like a KTX 1 or KTX 2 file.
pub fn is_ktx(data: &[u8]) -> bool {
	data.len() >= 12 && (data[..12] == KTX2_IDENTIFIER || data[..12] == KTX1_IDENTIFIER)
}

/// Whether the file at `path` starts like a KTX file.
pub fn file_is_ktx(path: &std::path::Path) -> bool {
	let Ok(mut file) = std::fs::File::open(path) else {
		return false;
	};

	let mut header = [0u8; 12];

	file.read_exact(&mut header).is_ok() && is_ktx(&header)
}

impl KtxImage {
	/// The size of a mip level.
	pub fn mip_dimensions(&self, level: u32) -> (u32, u32) {
		((self.width >> level).max(1), (self.height >> level).max(1))
	}

	/// `count` levels from `first` laid end to end, as the image upload takes them. A `count` of
	/// zero, or one that runs past the last level, takes every level from `first`. `None` if there
	/// is no level to start from or the levels hold no pixels.
	pub fn chain(&self, first: usize, count: usize) -> Option<(Vec<u8>, usize)> {
		if first >= self.levels.len() {
			return None;
		}

		let count = if count == 0 || first + count > self.levels.len() {
			self.levels.len() - first
		} else {
			count
		};

		let pixels: Vec<u8> = self.levels[first..first + count].concat();

		(!pixels.is_empty()).then_some((pixels, count))
	}
}

/// Reads a KTX (version 1 or 2) texture. Only plain 2D textures of 8-bit RGBA, BGRA or R are
/// accepted, and the pixels are not Basis compressed.
pub fn read_ktx(data: &[u8]) -> Result<KtxImage, KtxError> {
	if data.len() >= 12 && data[..12] == KTX2_IDENTIFIER {
		read_ktx2(data)
	} else if data.len() >= 12 && data[..12] == KTX1_IDENTIFIER {
		read_ktx1(data)
	} else {
		Err(KtxError::Invalid("the data is not a KTX file".to_owned()))
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	fn image(levels: &[usize]) -> KtxImage {
		KtxImage {
			format: FORMAT_RGBA8_UNORM,
			width: 8,
			height: 4,
			levels: levels.iter().map(|size| vec![*size as u8; *size]).collect(),
		}
	}

	#[test]
	fn both_ktx_versions_are_recognized_and_nothing_else() {
		assert!(is_ktx(&KTX1_IDENTIFIER));
		assert!(is_ktx(&KTX2_IDENTIFIER));
		assert!(!is_ktx(&KTX2_IDENTIFIER[..11]));
		assert!(!is_ktx(b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR"));
	}

	#[test]
	fn a_missing_file_is_not_ktx() {
		assert!(!file_is_ktx(std::path::Path::new("/no/such/file.ktx")));
	}

	#[test]
	fn mip_dimensions_halve_down_to_one() {
		let image = image(&[1]);

		assert_eq!(image.mip_dimensions(0), (8, 4));
		assert_eq!(image.mip_dimensions(2), (2, 1));
		assert_eq!(image.mip_dimensions(9), (1, 1));
	}

	#[test]
	fn a_chain_joins_levels_and_clamps_the_count() {
		let image = image(&[3, 2, 1]);

		assert_eq!(image.chain(0, 0), Some((vec![3, 3, 3, 2, 2, 1], 3)));
		assert_eq!(image.chain(1, 1), Some((vec![2, 2], 1)));
		assert_eq!(image.chain(1, 9), Some((vec![2, 2, 1], 2)));
		assert_eq!(image.chain(3, 0), None);
	}

	#[test]
	fn a_chain_with_no_pixels_is_nothing() {
		assert_eq!(image(&[0]).chain(0, 0), None);
	}

	fn ktx2_file(
		vk_format: u32,
		supercompression: u32,
		width: u32,
		height: u32,
		levels: &[Vec<u8>],
	) -> Vec<u8> {
		let level_count = levels.len() as u32;
		let index_end = 80 + 24 * levels.len();

		// A minimal data format descriptor: its length and nothing else
		let dfd_offset = index_end;
		let dfd = 4u32.to_le_bytes();
		let data_start = (dfd_offset + dfd.len()).next_multiple_of(8);

		let mut offsets = Vec::new();
		let mut position = data_start;

		for level in levels {
			offsets.push(position);
			position += level.len().next_multiple_of(8);
		}

		let mut out = Vec::new();

		out.extend(KTX2_IDENTIFIER);

		for value in [
			vk_format,
			1,
			width,
			height,
			0,
			0,
			1,
			level_count,
			supercompression,
		] {
			out.extend(value.to_le_bytes());
		}

		for value in [dfd_offset as u32, dfd.len() as u32, 0, 0] {
			out.extend(value.to_le_bytes());
		}

		for value in [0u64, 0u64] {
			out.extend(value.to_le_bytes());
		}

		for (level, offset) in levels.iter().zip(&offsets) {
			out.extend((*offset as u64).to_le_bytes());
			out.extend((level.len() as u64).to_le_bytes());
			out.extend((level.len() as u64).to_le_bytes());
		}

		out.extend(dfd);
		out.resize(data_start, 0);

		for level in levels {
			out.extend(level);
			out.resize(out.len().next_multiple_of(8), 0);
		}

		out
	}

	fn pixels(size: usize, seed: u8) -> Vec<u8> {
		(0..size)
			.map(|index| (index as u8).wrapping_mul(7).wrapping_add(seed))
			.collect()
	}

	#[test]
	fn a_ktx2_texture_gives_its_levels_in_order() {
		let levels = vec![pixels(64, 1), pixels(16, 2), pixels(4, 3)];
		let file = ktx2_file(37, 0, 4, 4, &levels);

		let image = read_ktx(&file).unwrap();

		assert_eq!(
			(image.format, image.width, image.height),
			(FORMAT_RGBA8_UNORM, 4, 4)
		);
		assert_eq!(image.levels.len(), 3);
		assert_eq!(image.levels, levels);
	}

	#[test]
	fn the_vulkan_formats_map_to_the_engine_formats() {
		for (vk, expected, bytes) in [
			(37, FORMAT_RGBA8_UNORM, 64),
			(43, FORMAT_RGBA8_SRGB, 64),
			(44, FORMAT_BGRA8_UNORM, 64),
			(9, FORMAT_R8_UNORM, 16),
		] {
			let image = read_ktx(&ktx2_file(vk, 0, 4, 4, &[pixels(bytes, 0)])).unwrap();

			assert_eq!(image.format, expected, "vk format {vk}");
		}
	}

	#[test]
	fn an_odd_sized_level_is_sized_to_a_whole_pixel_with_no_row_padding() {
		let file = ktx2_file(37, 0, 3, 3, &[pixels(3 * 3 * 4, 0), pixels(4, 1)]);

		let image = read_ktx(&file).unwrap();

		assert_eq!(image.levels[0].len(), 36);
		assert_eq!(image.levels[1].len(), 4);
	}

	#[test]
	fn other_formats_are_not_supported() {
		assert_eq!(
			read_ktx(&ktx2_file(97, 0, 4, 4, &[pixels(128, 0)])),
			Err(KtxError::UnsupportedFormat)
		);
	}

	#[test]
	fn an_undefined_format_is_taken_to_need_transcoding() {
		assert_eq!(
			read_ktx(&ktx2_file(0, 0, 4, 4, &[pixels(16, 0)])),
			Err(KtxError::NeedsTranscoding)
		);
	}

	#[test]
	fn basis_supercompression_needs_transcoding() {
		assert_eq!(
			read_ktx(&ktx2_file(37, 1, 4, 4, &[pixels(64, 0)])),
			Err(KtxError::NeedsTranscoding)
		);
	}

	#[test]
	fn zlib_supercompressed_levels_are_inflated() {
		let raw = pixels(64, 9);
		let packed = miniz_oxide::deflate::compress_to_vec_zlib(&raw, 6);

		let mut file = ktx2_file(37, 3, 4, 4, &[packed.clone()]);

		// The level index says how big the level is once it is inflated
		let level_index = 80;
		file[level_index + 16..level_index + 24].copy_from_slice(&(raw.len() as u64).to_le_bytes());

		let image = read_ktx(&file).unwrap();

		assert_eq!(image.levels[0], raw);
	}

	#[test]
	fn a_non_2d_texture_is_rejected() {
		let mut file = ktx2_file(37, 0, 4, 4, &[pixels(64, 0)]);

		// face count of 6
		file[36..40].copy_from_slice(&6u32.to_le_bytes());

		assert_eq!(read_ktx(&file), Err(KtxError::NotPlain2d));

		let one_d = ktx2_file(37, 0, 4, 0, &[pixels(16, 0)]);

		assert_eq!(read_ktx(&one_d), Err(KtxError::NotPlain2d));
	}

	#[test]
	fn something_that_is_not_ktx_is_invalid() {
		assert!(matches!(
			read_ktx(b"not a texture at all"),
			Err(KtxError::Invalid(_))
		));
		assert!(matches!(read_ktx(&[]), Err(KtxError::Invalid(_))));
	}

	#[test]
	fn a_cut_short_ktx2_file_is_invalid() {
		let file = ktx2_file(37, 0, 4, 4, &[pixels(64, 0)]);

		assert!(matches!(read_ktx(&file[..90]), Err(KtxError::Invalid(_))));
	}

	fn ktx1_file(
		internal_format: u32,
		width: u32,
		height: u32,
		levels: &[Vec<u8>],
		big_endian: bool,
	) -> Vec<u8> {
		let word = |value: u32| {
			if big_endian {
				value.to_be_bytes()
			} else {
				value.to_le_bytes()
			}
		};

		let mut out = Vec::new();

		out.extend(KTX1_IDENTIFIER);
		out.extend(if big_endian {
			0x0403_0201u32.to_be_bytes()
		} else {
			0x0403_0201u32.to_le_bytes()
		});

		for value in [
			0x1401,
			1,
			0x1908,
			internal_format,
			0x1908,
			width,
			height,
			0,
			0,
			1,
			levels.len() as u32,
			0,
		] {
			out.extend(word(value));
		}

		for level in levels {
			out.extend(word(level.len() as u32));
			out.extend(level);
			out.resize(out.len().next_multiple_of(4), 0);
		}

		out
	}

	#[test]
	fn a_ktx1_texture_is_read_with_its_mip_levels() {
		let levels = vec![pixels(4 * 4 * 4, 1), pixels(2 * 2 * 4, 2), pixels(4, 3)];

		let image = read_ktx(&ktx1_file(GL_SRGB8_ALPHA8, 4, 4, &levels, false)).unwrap();

		assert_eq!(image.format, FORMAT_RGBA8_SRGB);
		assert_eq!(image.levels, levels);
	}

	#[test]
	fn a_big_endian_ktx1_texture_is_read_too() {
		let levels = vec![pixels(2 * 2 * 4, 5), pixels(4, 6)];

		let image = read_ktx(&ktx1_file(GL_RGBA8, 2, 2, &levels, true)).unwrap();

		assert_eq!(
			(image.format, image.width, image.levels),
			(FORMAT_RGBA8_UNORM, 2, levels)
		);
	}

	#[test]
	fn ktx1_formats_other_than_rgba8_are_unsupported() {
		assert_eq!(
			read_ktx(&ktx1_file(0x8051, 2, 2, &[pixels(12, 0)], false)),
			Err(KtxError::UnsupportedFormat)
		);
	}
}
