use std::path::PathBuf;
use std::sync::OnceLock;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PathQuery
{
	Shaders,
	DataPacks,
}

pub fn base_dir() -> &'static PathBuf
{
	static BASE: OnceLock<PathBuf> = OnceLock::new();

	BASE.get_or_init(|| {
		if let Some(dir) = std::env::var_os("RAPTOR_BASE_DIR") {
			return PathBuf::from(dir);
		}

		PathBuf::from(env!("CARGO_MANIFEST_DIR"))
			.ancestors()
			.nth(2)
			.map_or_else(|| PathBuf::from("."), PathBuf::from)
	})
}

pub fn asset_path(query: PathQuery) -> PathBuf
{
	match query {
		PathQuery::Shaders => base_dir().join("Shaders"),
		PathQuery::DataPacks => base_dir().join("build"),
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn paths_end_in_expected_directories()
	{
		assert!(asset_path(PathQuery::Shaders).ends_with("Shaders"));
		assert!(asset_path(PathQuery::DataPacks).ends_with("build"));
	}
}
