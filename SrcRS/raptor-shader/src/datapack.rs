use std::fs;
use std::io;
use std::path::Path;

pub const HEADER_START: u16 = 0xA1B2;
pub const HEADER_END: u16 = 0x2B1A;

const ENTRY_HEADER_SIZE: usize = 16;

/// A file of entries that are found by a 64 bit id, which holds the compiled shader programs.
///
/// ```text
/// HEADER BEGIN - A1B2 (2 bytes), number of entries (2 bytes)
///     ENTRY 1: id (8 bytes), offset (4 bytes), size (4 bytes)
///     ENTRY 2
///     ENTRY N
/// HEADER END   - 2B1A (2 bytes)
/// ENTRY 1 DATA
/// ENTRY 2 DATA
/// ENTRY N DATA
/// ```
///
/// Numbers are in the byte order of the machine, like the file always has been.
#[derive(Default)]
pub struct DataPack
{
	entries: Vec<Entry>,
}

struct Entry
{
	id: u64,
	data: Vec<u8>,
}

/// What reading a pack found.
#[derive(Debug, PartialEq, Eq)]
pub enum ReadOutcome
{
	Loaded,
	/// The file could not be opened
	Missing,
	/// The file is not a pack. It is emptied so that a good one can be written over it.
	Corrupt,
}

fn read_u16(bytes: &[u8], at: usize) -> Option<u16>
{
	Some(u16::from_ne_bytes(bytes.get(at..at + 2)?.try_into().ok()?))
}

fn read_u32(bytes: &[u8], at: usize) -> Option<u32>
{
	Some(u32::from_ne_bytes(bytes.get(at..at + 4)?.try_into().ok()?))
}

fn read_u64(bytes: &[u8], at: usize) -> Option<u64>
{
	Some(u64::from_ne_bytes(bytes.get(at..at + 8)?.try_into().ok()?))
}

impl DataPack
{
	/// Reads a pack from its bytes, or returns none if the header is not one. An entry that reaches
	/// past the end of the bytes gets the part that is there.
	pub fn parse(bytes: &[u8]) -> Option<Self>
	{
		if read_u16(bytes, 0)? != HEADER_START {
			return None;
		}

		let count = usize::from(read_u16(bytes, 2)?);
		let header_end = 4 + count * ENTRY_HEADER_SIZE;

		if read_u16(bytes, header_end)? != HEADER_END {
			return None;
		}

		let mut entries = Vec::with_capacity(count);

		for index in 0..count {
			let at = 4 + index * ENTRY_HEADER_SIZE;

			let id = read_u64(bytes, at)?;
			let offset = read_u32(bytes, at + 8)? as usize;
			let size = read_u32(bytes, at + 12)? as usize;

			let start = offset.min(bytes.len());
			let end = offset.saturating_add(size).min(bytes.len());

			entries.push(Entry {
				id,
				data: bytes[start..end].to_vec(),
			});
		}

		Some(Self { entries })
	}

	/// Reads the pack at `path`, replacing what is in this one.
	pub fn read_file(&mut self, path: &Path) -> ReadOutcome
	{
		self.entries.clear();

		let Ok(bytes) = fs::read(path) else {
			return ReadOutcome::Missing;
		};

		match Self::parse(&bytes) {
			Some(pack) => {
				*self = pack;
				ReadOutcome::Loaded
			}
			None => {
				let _ = fs::write(path, []);
				ReadOutcome::Corrupt
			}
		}
	}

	pub fn to_bytes(&self) -> Vec<u8>
	{
		let header_size = 4 + self.entries.len() * ENTRY_HEADER_SIZE + 2;

		let mut bytes = Vec::with_capacity(
			header_size
				+ self
					.entries
					.iter()
					.map(|entry| entry.data.len())
					.sum::<usize>(),
		);

		bytes.extend_from_slice(&HEADER_START.to_ne_bytes());
		bytes.extend_from_slice(&(self.entries.len() as u16).to_ne_bytes());

		let mut offset = header_size as u32;

		for entry in &self.entries {
			bytes.extend_from_slice(&entry.id.to_ne_bytes());
			bytes.extend_from_slice(&offset.to_ne_bytes());
			bytes.extend_from_slice(&(entry.data.len() as u32).to_ne_bytes());

			offset += entry.data.len() as u32;
		}

		bytes.extend_from_slice(&HEADER_END.to_ne_bytes());

		for entry in &self.entries {
			bytes.extend_from_slice(&entry.data);
		}

		bytes
	}

	pub fn write_file(&self, path: &Path) -> io::Result<()>
	{
		fs::write(path, self.to_bytes())
	}

	/// Adds an entry, or replaces the data of the one with the same id. Returns true if one was
	/// replaced.
	pub fn add_entry(&mut self, id: u64, data: &[u8]) -> bool
	{
		if let Some(entry) = self.entries.iter_mut().find(|entry| entry.id == id) {
			entry.data = data.to_vec();
			return true;
		}

		self.entries.push(Entry {
			id,
			data: data.to_vec(),
		});

		false
	}

	pub fn get(&self, id: u64) -> Option<&[u8]>
	{
		self.entries
			.iter()
			.find(|entry| entry.id == id)
			.map(|entry| entry.data.as_slice())
	}

	pub fn len(&self) -> usize
	{
		self.entries.len()
	}

	pub fn is_empty(&self) -> bool
	{
		self.entries.is_empty()
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn sample() -> DataPack
	{
		let mut pack = DataPack::default();

		pack.add_entry(0x1111, b"first");
		pack.add_entry(0x2222, b"");
		pack.add_entry(u64::MAX - 1, b"third entry");

		pack
	}

	#[test]
	fn a_pack_survives_being_written_and_read()
	{
		let bytes = sample().to_bytes();
		let pack = DataPack::parse(&bytes).unwrap();

		assert_eq!(pack.len(), 3);
		assert_eq!(pack.get(0x1111), Some(&b"first"[..]));
		assert_eq!(pack.get(0x2222), Some(&b""[..]));
		assert_eq!(pack.get(u64::MAX - 1), Some(&b"third entry"[..]));
		assert_eq!(pack.get(7), None);
	}

	#[test]
	fn the_bytes_have_the_header_the_cpp_wrote()
	{
		let bytes = sample().to_bytes();

		assert_eq!(read_u16(&bytes, 0), Some(HEADER_START));
		assert_eq!(read_u16(&bytes, 2), Some(3));

		let header_size = 4 + 3 * 16 + 2;

		assert_eq!(read_u16(&bytes, header_size - 2), Some(HEADER_END));
		assert_eq!(read_u64(&bytes, 4), Some(0x1111));
		assert_eq!(read_u32(&bytes, 12), Some(header_size as u32));
		assert_eq!(read_u32(&bytes, 16), Some(5));
		assert_eq!(read_u32(&bytes, 4 + 16 + 8), Some(header_size as u32 + 5));
		assert_eq!(&bytes[header_size..header_size + 5], b"first");
		assert_eq!(bytes.len(), header_size + 5 + 11);
	}

	#[test]
	fn adding_an_id_again_replaces_its_data()
	{
		let mut pack = sample();

		assert!(pack.add_entry(0x1111, b"changed"));
		assert!(!pack.add_entry(0x3333, b"new"));

		assert_eq!(pack.len(), 4);
		assert_eq!(pack.get(0x1111), Some(&b"changed"[..]));
	}

	#[test]
	fn a_bad_header_is_not_a_pack()
	{
		let mut bytes = sample().to_bytes();

		assert!(DataPack::parse(&[]).is_none());
		assert!(DataPack::parse(&bytes[..3]).is_none());

		bytes[0] ^= 1;
		assert!(DataPack::parse(&bytes).is_none());

		let mut bytes = sample().to_bytes();
		let end = 4 + 3 * 16;
		bytes[end] ^= 1;
		assert!(DataPack::parse(&bytes).is_none());
	}

	#[test]
	fn an_entry_past_the_end_of_a_short_file_gets_what_is_there()
	{
		let bytes = sample().to_bytes();
		let pack = DataPack::parse(&bytes[..bytes.len() - 4]).unwrap();

		assert_eq!(pack.get(u64::MAX - 1), Some(&b"third e"[..]));
	}

	#[test]
	fn a_corrupt_file_is_emptied_and_a_missing_one_is_reported()
	{
		let dir = std::env::temp_dir().join(format!("raptor-datapack-{}", std::process::id()));
		fs::create_dir_all(&dir).unwrap();

		let path = dir.join("shaders.spack");

		let mut pack = DataPack::default();
		assert_eq!(pack.read_file(&path), ReadOutcome::Missing);

		fs::write(&path, b"this is not a pack").unwrap();
		assert_eq!(pack.read_file(&path), ReadOutcome::Corrupt);
		assert_eq!(fs::read(&path).unwrap().len(), 0);

		sample().write_file(&path).unwrap();
		assert_eq!(pack.read_file(&path), ReadOutcome::Loaded);
		assert_eq!(pack.len(), 3);

		let _ = fs::remove_dir_all(&dir);
	}

	/// The packs the C++ wrote are read back to the bytes they were, and every program in them is
	/// whole.
	#[test]
	fn the_packs_in_the_repository_read_and_write_back_the_same()
	{
		let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../cache/shaders/Spirv");

		let Ok(listing) = fs::read_dir(&dir) else {
			return;
		};

		for file in listing.flatten() {
			if file
				.path()
				.extension()
				.is_none_or(|extension| extension != "spack")
			{
				continue;
			}

			let bytes = fs::read(file.path()).unwrap();
			let pack = DataPack::parse(&bytes).unwrap_or_else(|| panic!("{:?}", file.path()));

			assert_eq!(pack.to_bytes(), bytes, "{:?}", file.path());

			for entry in &pack.entries {
				let blob = crate::program::ProgramBlob::parse(&entry.data).unwrap();

				assert_eq!(blob.spirv.len() % 4, 0);
				assert_eq!(&blob.spirv[..4], &0x0723_0203u32.to_ne_bytes());
			}
		}
	}
}
