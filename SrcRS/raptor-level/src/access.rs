use raptor_config::host::{CATEGORY_CORE, Host, LogLevel};
use raptor_config::model::{Entry, Kind, Primitive};

/// A config host that reads includes from the file system and logs through the engine's log.
pub struct FsHost;

impl Host for FsHost
{
	fn read_include(&mut self, path: &[u8], extension: &[u8]) -> Option<Vec<u8>>
	{
		let mut name = String::from_utf8_lossy(path).into_owned();

		if std::path::Path::new(&name).extension().is_none() {
			name.push_str(&String::from_utf8_lossy(extension));
		}

		std::fs::read(name).ok()
	}

	fn log(&mut self, level: LogLevel, _category: i32, message: &[u8])
	{
		let message = String::from_utf8_lossy(message);

		match level {
			LogLevel::Error => raptor_core::log_error!("{message}"),
			LogLevel::Warning => raptor_core::log_warn!("{message}"),
			_ => raptor_core::log_info!("{message}"),
		}
	}
}

/// Reads typed values out of config entries the way the engine's `ConfigEntry` getters do: a number
/// reads as either kind, and anything else warns and reads as zero.
pub struct Reader<'a>
{
	host: &'a mut dyn Host,
}

pub fn member<'e>(parent: &'e Entry, name: &str) -> Option<&'e Entry>
{
	parent
		.members
		.iter()
		.find(|member| member.name == name.as_bytes())
}

pub fn find<'e>(entries: &'e [Entry], name: &str) -> Option<&'e Entry>
{
	entries.iter().find(|entry| entry.name == name.as_bytes())
}

impl<'a> Reader<'a>
{
	pub fn new(host: &'a mut dyn Host) -> Self
	{
		Self { host }
	}

	pub fn warn(&mut self, message: &str)
	{
		self.host
			.log(LogLevel::Warning, CATEGORY_CORE, message.as_bytes());
	}

	pub fn error(&mut self, message: &str)
	{
		self.host
			.log(LogLevel::Error, CATEGORY_CORE, message.as_bytes());
	}

	pub fn float_of(&mut self, primitive: &Primitive) -> f32
	{
		match primitive.kind {
			Kind::Float => primitive.float_value,
			Kind::Int => primitive.int_value as f32,
			_ => {
				self.warn("Attempting to retrieve float type from non-float/int!");
				0.0
			}
		}
	}

	pub fn int_of(&mut self, primitive: &Primitive) -> i64
	{
		match primitive.kind {
			Kind::Int => primitive.int_value,
			Kind::Float => primitive.float_value as i64,
			_ => {
				self.warn("Attempting to retrieve int type from non-int/float!");
				0
			}
		}
	}

	pub fn float(&mut self, parent: &Entry, name: &str) -> Option<f32>
	{
		member(parent, name).map(|entry| self.float_of(&entry.value))
	}

	pub fn int(&mut self, parent: &Entry, name: &str) -> Option<i64>
	{
		member(parent, name).map(|entry| self.int_of(&entry.value))
	}

	/// A flag the engine stores as the number 1
	pub fn flag(&mut self, parent: &Entry, name: &str) -> bool
	{
		self.int(parent, name).is_some_and(|value| value as i32 == 1)
	}

	fn array_floats<const N: usize>(&mut self, entry: &Entry, fallback: [f32; N]) -> [f32; N]
	{
		if !entry.is_array || entry.array.len() < N {
			self.warn(&format!(
				"Config entry '{}' is not an array of at least {N} values",
				String::from_utf8_lossy(&entry.name)
			));
			return fallback;
		}

		let mut values = [0.0; N];

		for (value, primitive) in values.iter_mut().zip(&entry.array) {
			*value = self.float_of(primitive);
		}

		values
	}

	pub fn vec3(&mut self, parent: &Entry, name: &str) -> Option<[f32; 3]>
	{
		member(parent, name).map(|entry| self.array_floats(entry, [0.0; 3]))
	}

	pub fn quat(&mut self, parent: &Entry, name: &str) -> Option<[f32; 4]>
	{
		member(parent, name).map(|entry| self.array_floats(entry, [0.0, 0.0, 0.0, 1.0]))
	}

	pub fn color(&mut self, parent: &Entry, name: &str) -> Option<[i32; 4]>
	{
		let entry = member(parent, name)?;

		if !entry.is_array || entry.array.len() < 4 {
			self.warn(&format!(
				"Config entry '{}' is not an array of at least 4 values",
				String::from_utf8_lossy(&entry.name)
			));
			return Some([0, 0, 0, 255]);
		}

		let mut color = [0; 4];

		for (value, primitive) in color.iter_mut().zip(&entry.array) {
			*value = self.int_of(primitive) as i32;
		}

		Some(color)
	}
}

pub fn float_array(name: &str, values: &[f32]) -> Entry
{
	Entry {
		name: name.as_bytes().to_vec(),
		value: Primitive {
			kind: Kind::Float,
			..Primitive::default()
		},
		is_array: true,
		array: values.iter().map(|&value| Primitive::float(value)).collect(),
		..Entry::default()
	}
}

pub fn int_array(name: &str, values: &[i32]) -> Entry
{
	Entry {
		name: name.as_bytes().to_vec(),
		value: Primitive {
			kind: Kind::Int,
			..Primitive::default()
		},
		is_array: true,
		array: values
			.iter()
			.map(|&value| Primitive::int(i64::from(value)))
			.collect(),
		..Entry::default()
	}
}

pub fn literal(name: &str, value: Primitive) -> Entry
{
	Entry {
		name: name.as_bytes().to_vec(),
		value,
		..Entry::default()
	}
}

pub fn structure(name: &str) -> Entry
{
	literal(name, Primitive::structure())
}

pub fn dot_reference(name: &str, reference: &str) -> Entry
{
	Entry {
		name: name.as_bytes().to_vec(),
		value: Primitive::string(reference.as_bytes()),
		is_dot_reference: true,
		..Entry::default()
	}
}
