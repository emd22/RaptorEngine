use std::collections::BTreeMap;
use std::fmt;
use std::sync::{OnceLock, RwLock};

#[derive(Clone, Debug, PartialEq)]
pub enum CVarValue
{
	Int(i64),
	Float(f32),
	Bool(bool),
	Str(String),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CVarKind
{
	Int,
	Float,
	Bool,
	Str,
}

impl CVarKind
{
	pub const fn name(self) -> &'static str
	{
		match self {
			CVarKind::Int => "Int",
			CVarKind::Float => "Float",
			CVarKind::Bool => "Boolean",
			CVarKind::Str => "String",
		}
	}
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum CVarError
{
	TypeMismatch
	{
		name: String,
		found: CVarKind,
		expected: CVarKind,
	},
	Undefined(String),
	InvalidValue(String),
}

impl fmt::Display for CVarError
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		match self {
			CVarError::TypeMismatch {
				name,
				found,
				expected,
			} => {
				write!(
					f,
					"Cannot assign type {} to CVar '{}' of type {}",
					expected.name(),
					name,
					found.name()
				)
			}
			CVarError::Undefined(name) => write!(f, "CVar '{name}' is not defined"),
			CVarError::InvalidValue(value) => write!(f, "'{value}' is not a valid value"),
		}
	}
}

impl std::error::Error for CVarError {}

impl CVarValue
{
	pub fn kind(&self) -> CVarKind
	{
		match self {
			CVarValue::Int(_) => CVarKind::Int,
			CVarValue::Float(_) => CVarKind::Float,
			CVarValue::Bool(_) => CVarKind::Bool,
			CVarValue::Str(_) => CVarKind::Str,
		}
	}

	pub fn as_int(&self) -> Option<i64>
	{
		match self {
			CVarValue::Int(value) => Some(*value),
			CVarValue::Bool(value) => Some(i64::from(*value)),
			CVarValue::Float(value) => Some(*value as i64),
			CVarValue::Str(_) => None,
		}
	}

	pub fn as_float(&self) -> Option<f32>
	{
		match self {
			CVarValue::Float(value) => Some(*value),
			CVarValue::Int(value) => Some(*value as f32),
			CVarValue::Bool(value) => Some(f32::from(u8::from(*value))),
			CVarValue::Str(_) => None,
		}
	}

	pub fn as_bool(&self) -> Option<bool>
	{
		self.as_int().map(|value| value != 0)
	}

	pub fn set_from_str(&mut self, text: &str) -> Result<(), CVarError>
	{
		let invalid = || CVarError::InvalidValue(text.to_owned());

		match self {
			CVarValue::Str(value) => *value = text.to_owned(),
			CVarValue::Int(value) => *value = text.parse().map_err(|_| invalid())?,
			CVarValue::Float(value) => *value = text.parse().map_err(|_| invalid())?,
			CVarValue::Bool(value) => {
				*value = match text {
					"true" | "1" => true,
					"false" | "0" => false,
					_ => return Err(invalid()),
				}
			}
		}

		Ok(())
	}
}

impl fmt::Display for CVarValue
{
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
	{
		match self {
			CVarValue::Int(value) => write!(f, "{value}"),
			CVarValue::Float(value) => write!(f, "{value}"),
			CVarValue::Bool(value) => write!(f, "{value}"),
			CVarValue::Str(value) => f.write_str(value),
		}
	}
}

#[derive(Default)]
pub struct CVarManager
{
	cvars: BTreeMap<String, CVarValue>,
}

impl CVarManager
{
	pub fn new() -> CVarManager
	{
		CVarManager::default()
	}

	pub fn set(&mut self, name: &str, value: CVarValue) -> Result<(), CVarError>
	{
		match self.cvars.get_mut(name) {
			Some(existing) if existing.kind() != value.kind() => Err(CVarError::TypeMismatch {
				name: name.to_owned(),
				found: existing.kind(),
				expected: value.kind(),
			}),
			Some(existing) => {
				*existing = value;
				Ok(())
			}
			None => {
				self.cvars.insert(name.to_owned(), value);
				Ok(())
			}
		}
	}

	pub fn set_int(&mut self, name: &str, value: i64) -> Result<(), CVarError>
	{
		self.set(name, CVarValue::Int(value))
	}

	pub fn set_float(&mut self, name: &str, value: f32) -> Result<(), CVarError>
	{
		self.set(name, CVarValue::Float(value))
	}

	pub fn set_bool(&mut self, name: &str, value: bool) -> Result<(), CVarError>
	{
		self.set(name, CVarValue::Bool(value))
	}

	pub fn set_string(&mut self, name: &str, value: &str) -> Result<(), CVarError>
	{
		self.set(name, CVarValue::Str(value.to_owned()))
	}

	pub fn set_from_str(&mut self, name: &str, text: &str) -> Result<(), CVarError>
	{
		self.cvars
			.get_mut(name)
			.ok_or_else(|| CVarError::Undefined(name.to_owned()))?
			.set_from_str(text)
	}

	pub fn get(&self, name: &str) -> Option<&CVarValue>
	{
		self.cvars.get(name)
	}

	pub fn int(&self, name: &str, fallback: i64) -> i64
	{
		self.get(name)
			.and_then(CVarValue::as_int)
			.unwrap_or(fallback)
	}

	pub fn float(&self, name: &str, fallback: f32) -> f32
	{
		self.get(name)
			.and_then(CVarValue::as_float)
			.unwrap_or(fallback)
	}

	pub fn bool(&self, name: &str, fallback: bool) -> bool
	{
		self.get(name)
			.and_then(CVarValue::as_bool)
			.unwrap_or(fallback)
	}

	pub fn string(&self, name: &str, fallback: &str) -> String
	{
		match self.get(name) {
			Some(CVarValue::Str(value)) => value.clone(),
			_ => fallback.to_owned(),
		}
	}

	pub fn iter(&self) -> impl Iterator<Item = (&str, &CVarValue)>
	{
		self.cvars
			.iter()
			.map(|(name, value)| (name.as_str(), value))
	}

	pub fn len(&self) -> usize
	{
		self.cvars.len()
	}

	pub fn is_empty(&self) -> bool
	{
		self.cvars.is_empty()
	}
}

static GLOBAL: OnceLock<RwLock<CVarManager>> = OnceLock::new();

pub fn global() -> &'static RwLock<CVarManager>
{
	GLOBAL.get_or_init(|| RwLock::new(CVarManager::new()))
}

fn read<R>(f: impl FnOnce(&CVarManager) -> R) -> R
{
	f(&global()
		.read()
		.unwrap_or_else(|poisoned| poisoned.into_inner()))
}

fn write(name: &str, value: CVarValue) -> Result<(), CVarError>
{
	let result = global()
		.write()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
		.set(name, value);

	if let Err(error) = &result {
		crate::log_error!(Core; "{error}");
	}

	result
}

pub fn set_int(name: &str, value: i64) -> Result<(), CVarError>
{
	write(name, CVarValue::Int(value))
}

pub fn set_float(name: &str, value: f32) -> Result<(), CVarError>
{
	write(name, CVarValue::Float(value))
}

pub fn set_bool(name: &str, value: bool) -> Result<(), CVarError>
{
	write(name, CVarValue::Bool(value))
}

pub fn set_string(name: &str, value: &str) -> Result<(), CVarError>
{
	write(name, CVarValue::Str(value.to_owned()))
}

pub fn int(name: &str, fallback: i64) -> i64
{
	read(|cvars| cvars.int(name, fallback))
}

pub fn float(name: &str, fallback: f32) -> f32
{
	read(|cvars| cvars.float(name, fallback))
}

pub fn bool(name: &str, fallback: bool) -> bool
{
	read(|cvars| cvars.bool(name, fallback))
}

pub fn string(name: &str, fallback: &str) -> String
{
	read(|cvars| cvars.string(name, fallback))
}

pub fn snapshot() -> Vec<(String, CVarValue)>
{
	read(|cvars| {
		cvars
			.iter()
			.map(|(name, value)| (name.to_owned(), value.clone()))
			.collect()
	})
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn set_get_and_type_checks()
	{
		let mut cvars = CVarManager::new();

		cvars.set_int("a", 3).unwrap();
		cvars.set_float("b", 1.5).unwrap();
		cvars.set_string("c", "x").unwrap();

		assert_eq!(cvars.int("a", 0), 3);
		assert_eq!(cvars.float("b", 0.0), 1.5);
		assert_eq!(cvars.string("c", ""), "x");
		assert_eq!(cvars.int("missing", 9), 9);
		assert!(matches!(
			cvars.set_float("a", 1.0),
			Err(CVarError::TypeMismatch { .. })
		));
		assert_eq!(cvars.int("a", 0), 3);
	}

	#[test]
	fn parsing_from_text()
	{
		let mut cvars = CVarManager::new();

		cvars.set_int("i", 0).unwrap();
		cvars.set_bool("b", false).unwrap();

		cvars.set_from_str("i", "42").unwrap();
		cvars.set_from_str("b", "true").unwrap();

		assert_eq!(cvars.int("i", 0), 42);
		assert!(cvars.bool("b", false));
		assert!(cvars.set_from_str("i", "4x").is_err());
		assert!(cvars.set_from_str("b", "yes").is_err());
		assert!(matches!(
			cvars.set_from_str("nope", "1"),
			Err(CVarError::Undefined(_))
		));
		assert_eq!(cvars.get("i").unwrap().to_string(), "42");
	}

	#[test]
	fn iteration_is_sorted()
	{
		let mut cvars = CVarManager::new();

		cvars.set_int("z", 1).unwrap();
		cvars.set_int("a", 1).unwrap();

		let names: Vec<_> = cvars.iter().map(|(name, _)| name).collect();
		assert_eq!(names, ["a", "z"]);
	}
}
