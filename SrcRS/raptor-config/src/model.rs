#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Kind
{
	None = 0,
	Int = 1,
	Float = 2,
	String = 3,
	Struct = 4,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Primitive
{
	pub kind: Kind,
	pub int_value: i64,
	pub float_value: f32,
	pub string_value: Option<Vec<u8>>,
}

impl Default for Primitive
{
	fn default() -> Self
	{
		Self {
			kind: Kind::None,
			int_value: 0,
			float_value: 0.0,
			string_value: None,
		}
	}
}

impl Primitive
{
	pub fn int(value: i64) -> Self
	{
		Self {
			kind: Kind::Int,
			int_value: value,
			..Self::default()
		}
	}

	pub fn float(value: f32) -> Self
	{
		Self {
			kind: Kind::Float,
			float_value: value,
			..Self::default()
		}
	}

	pub fn string(bytes: &[u8]) -> Self
	{
		let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
		Self {
			kind: Kind::String,
			string_value: Some(bytes[..end].to_vec()),
			..Self::default()
		}
	}

	pub fn structure() -> Self
	{
		Self {
			kind: Kind::Struct,
			..Self::default()
		}
	}

	pub fn duplicated(&self) -> Self
	{
		match self.kind {
			Kind::Int => Self::int(self.int_value),
			Kind::Float => Self::float(self.float_value),
			Kind::String => Self {
				kind: Kind::String,
				string_value: self.string_value.clone(),
				..Self::default()
			},
			Kind::None | Kind::Struct => Self {
				kind: self.kind,
				..Self::default()
			},
		}
	}
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct Entry
{
	pub name: Vec<u8>,
	pub value: Primitive,
	pub is_array: bool,
	pub is_dot_reference: bool,
	pub members: Vec<Entry>,
	pub array: Vec<Primitive>,
}

impl Entry
{
	pub fn add_member(&mut self, member: Entry)
	{
		self.value = Primitive::structure();
		self.members.push(member);
	}
}

#[derive(Debug, Default)]
pub struct Parsed
{
	pub entries: Vec<Entry>,
	pub has_errors: bool,
}
