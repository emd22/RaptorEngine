use std::fs::File;
use std::io::{self, BufWriter, Write};
use std::path::Path;

pub fn write_rgb(path: impl AsRef<Path>, width: u32, height: u32, pixels: &[u8]) -> io::Result<()>
{
	let expected = width as usize * height as usize * 3;

	if width == 0 || height == 0 || pixels.len() != expected {
		return Err(io::Error::new(
			io::ErrorKind::InvalidInput,
			format!(
				"expected {expected} bytes for a {width}x{height} RGB image, got {}",
				pixels.len()
			),
		));
	}

	let mut out = BufWriter::new(File::create(path)?);

	write!(out, "P6\n{width} {height}\n255\n")?;
	out.write_all(pixels)?;
	out.flush()
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn writes_header_and_pixels()
	{
		let path = std::env::temp_dir().join("raptor_core_ppm_test.ppm");

		write_rgb(&path, 2, 1, &[1, 2, 3, 4, 5, 6]).unwrap();

		assert_eq!(
			std::fs::read(&path).unwrap(),
			b"P6\n2 1\n255\n\x01\x02\x03\x04\x05\x06"
		);
		assert!(write_rgb(&path, 2, 2, &[0; 3]).is_err());
		let _ = std::fs::remove_file(path);
	}
}
