use std::fs;
use std::io;
use std::path::Path;

/// Writes `data` next to `path` first and then moves it into place, so an interrupted write never
/// leaves half a file behind.
pub fn write_atomic(path: &Path, data: &[u8]) -> io::Result<()>
{
	let mut temporary = path.as_os_str().to_owned();
	temporary.push(".tmp");
	let temporary = Path::new(&temporary);

	fs::write(temporary, data)?;

	fs::rename(temporary, path).inspect_err(|_| {
		let _ = fs::remove_file(temporary);
	})
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn the_file_is_replaced_and_no_temporary_is_left()
	{
		let directory = std::env::temp_dir().join(format!("raptor_level_save_{}", std::process::id()));
		fs::create_dir_all(&directory).unwrap();

		let path = directory.join("level.prx");
		fs::write(&path, b"old").unwrap();

		write_atomic(&path, b"new").unwrap();

		assert_eq!(fs::read(&path).unwrap(), b"new");
		assert!(!directory.join("level.prx.tmp").exists());

		fs::remove_dir_all(&directory).unwrap();
	}

	#[test]
	fn writing_into_a_missing_directory_fails()
	{
		let path = std::env::temp_dir().join("raptor_level_missing_dir/level.prx");

		assert!(write_atomic(&path, b"x").is_err());
	}
}
