use raptor_config::host::Host;
use raptor_config::model::{Entry, Kind};

use crate::access::{Reader, find, member};

/// One material of the list file. The texture paths are empty where the material has none.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MaterialDef
{
	pub name: String,
	pub diffuse: String,
	pub normal: String,
	pub orm: String,
}

#[derive(Debug, Default)]
pub struct MaterialList
{
	pub has_list: bool,
	pub entries: Vec<MaterialDef>,
}

fn text(reader: &mut Reader, parent: &Entry, name: &str, fallback: &str) -> String
{
	let Some(entry) = member(parent, name) else {
		return fallback.to_owned();
	};

	if entry.value.kind != Kind::String {
		reader.warn("Attempting to retrieve string from non-string config primitive");
		return fallback.to_owned();
	}

	String::from_utf8_lossy(entry.value.string_value.as_deref().unwrap_or(&[])).into_owned()
}

impl MaterialList
{
	pub fn from_entries(entries: &[Entry], host: &mut dyn Host) -> Self
	{
		let Some(list) = find(entries, "list") else {
			return Self::default();
		};

		let mut reader = Reader::new(host);

		let entries = list
			.members
			.iter()
			.map(|item| MaterialDef {
				name: text(&mut reader, item, "name", "unnamed"),
				diffuse: text(&mut reader, item, "diffuse", ""),
				normal: text(&mut reader, item, "normal", ""),
				orm: text(&mut reader, item, "orm", ""),
			})
			.collect();

		Self {
			has_list: true,
			entries,
		}
	}

	pub fn parse(data: &[u8], prelude_path: Option<&[u8]>, host: &mut dyn Host) -> Self
	{
		let parsed = raptor_config::parse(data, prelude_path, b".conf", host);

		Self::from_entries(&parsed.entries, host)
	}
}

/// The materials of a list as the engine knows them: each list index and the material ID created for
/// it. ID 0 is the null material.
#[derive(Debug, Default)]
pub struct MaterialRegistry
{
	slots: Vec<(String, u32)>,
}

impl MaterialRegistry
{
	pub fn new() -> Self
	{
		Self::default()
	}

	pub fn clear(&mut self)
	{
		self.slots.clear();
	}

	pub fn push(&mut self, name: &str, material: u32)
	{
		self.slots.push((name.to_owned(), material));
	}

	pub fn len(&self) -> usize
	{
		self.slots.len()
	}

	pub fn is_empty(&self) -> bool
	{
		self.slots.is_empty()
	}

	pub fn name(&self, index: usize) -> &str
	{
		self.slots
			.get(index)
			.map_or("unknown", |(name, _)| name.as_str())
	}

	pub fn material(&self, index: i32) -> u32
	{
		usize::try_from(index)
			.ok()
			.and_then(|index| self.slots.get(index))
			.map_or(0, |&(_, material)| material)
	}

	pub fn find(&self, material: u32) -> i32
	{
		if material == 0 {
			return -1;
		}

		self.slots
			.iter()
			.position(|&(_, id)| id == material)
			.map_or(-1, |index| index as i32)
	}
}

#[cfg(test)]
mod tests
{
	use super::*;
	use raptor_config::host::LogLevel;

	#[derive(Default)]
	struct TestHost
	{
		messages: Vec<String>,
	}

	impl Host for TestHost
	{
		fn read_include(&mut self, _path: &[u8], _extension: &[u8]) -> Option<Vec<u8>>
		{
			Some(Vec::new())
		}

		fn log(&mut self, _level: LogLevel, _category: i32, message: &[u8])
		{
			self.messages
				.push(String::from_utf8_lossy(message).into_owned());
		}
	}

	fn parse(text: &str) -> (MaterialList, TestHost)
	{
		let mut host = TestHost::default();
		let list = MaterialList::parse(text.as_bytes(), None, &mut host);

		(list, host)
	}

	#[test]
	fn entries_keep_their_order_and_missing_textures_are_empty()
	{
		let (list, host) = parse(
			"list = {\n\t$ = {\n\t\tname = \"white\"\n\t\tdiffuse = \"/w/d.ktx2\"\n\t}\n\t$ = {\n\t\tname = \"rock\"\n\t\tdiffuse = \"/r/d.ktx2\"\n\t\tnormal = \"/r/n.ktx2\"\n\t\torm = \"/r/o.ktx2\"\n\t}\n\t$ = {\n\t}\n}\n",
		);

		assert!(list.has_list && host.messages.is_empty());
		assert_eq!(list.entries.len(), 3);
		assert_eq!(list.entries[0].name, "white");
		assert_eq!(list.entries[0].diffuse, "/w/d.ktx2");
		assert!(list.entries[0].normal.is_empty() && list.entries[0].orm.is_empty());
		assert_eq!(list.entries[1].orm, "/r/o.ktx2");
		assert_eq!(list.entries[2].name, "unnamed");
	}

	#[test]
	fn a_file_without_a_list_says_so()
	{
		let (list, _) = parse("other = {\n}\n");

		assert!(!list.has_list && list.entries.is_empty());
	}

	#[test]
	fn a_non_string_texture_warns_and_reads_as_empty()
	{
		let (list, host) = parse("list = {\n\t$ = {\n\t\tname = \"a\"\n\t\tdiffuse = 4\n\t}\n}\n");

		assert!(list.entries[0].diffuse.is_empty());
		assert_eq!(host.messages.len(), 1);
	}

	#[test]
	fn registry_lookups_match_the_library()
	{
		let mut registry = MaterialRegistry::new();

		registry.push("a", 5);
		registry.push("b", 9);

		assert_eq!(registry.len(), 2);
		assert_eq!(registry.name(1), "b");
		assert_eq!(registry.name(2), "unknown");
		assert_eq!(registry.material(1), 9);
		assert_eq!(registry.material(-1), 0);
		assert_eq!(registry.material(2), 0);
		assert_eq!(registry.find(9), 1);
		assert_eq!(registry.find(0), -1);
		assert_eq!(registry.find(7), -1);
	}
}
