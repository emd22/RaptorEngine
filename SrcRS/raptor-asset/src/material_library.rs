use raptor_core::{log_error, log_info, log_warn};
use raptor_level::{MaterialList, MaterialRegistry};

use crate::fs::{ConfigHost, config_constants_path, read_resolved, resolve_path};
use crate::manager::{AssetManager, ImageRequest};
use crate::materials::{MaterialId, MaterialStore, ResourceKind};
use raptor_gpu::ImageFormat;

/// The materials of a list file, by list index
#[derive(Default)]
pub struct MaterialLibrary {
	registry: MaterialRegistry,
}

impl MaterialLibrary {
	pub fn new() -> Self {
		Self::default()
	}

	pub fn count(&self) -> usize {
		self.registry.len()
	}

	pub fn name(&self, index: usize) -> &str {
		self.registry.name(index)
	}

	pub fn material(&self, index: i32) -> MaterialId {
		MaterialId(self.registry.material(index))
	}

	pub fn find(&self, material: MaterialId) -> i32 {
		self.registry.find(material.0)
	}

	/// Reads the list at `list_path`, and makes a material for each entry with the textures under
	/// `texture_root`. Returns false if the file has no `list`.
	pub fn load(
		&mut self,
		store: &MaterialStore,
		assets: &AssetManager,
		list_path: &str,
		texture_root: &str,
	) -> bool {
		let Ok(bytes) = read_resolved(list_path) else {
			log_error!(Asset; "The material list '{list_path}' has no `list` entry");
			return false;
		};

		let constants = config_constants_path();

		let list = MaterialList::parse(
			&bytes,
			Some(constants.to_string_lossy().as_bytes()),
			&mut ConfigHost,
		);

		if !list.has_list {
			log_error!(Asset; "The material list '{list_path}' has no `list` entry");
			return false;
		}

		self.registry.clear();

		for def in &list.entries {
			let Some((id, material)) = store.create(&def.name, false) else {
				log_error!(Asset; "No room for the material '{}'", def.name);
				continue;
			};

			let textures = [
				(ResourceKind::Diffuse, &def.diffuse),
				(ResourceKind::Normal, &def.normal),
				(ResourceKind::Orm, &def.orm),
			];

			for (kind, relative) in textures {
				if relative.is_empty() {
					continue;
				}

				let path = format!("{texture_root}{relative}");

				if !resolve_path(&path).exists() {
					log_warn!(Asset; "Material '{}' is missing the texture '{path}'", def.name);
					continue;
				}

				let handle = assets.load_image(
					resolve_path(&path),
					ImageRequest::flat(ImageFormat::Rgba8UNorm),
				);

				material.attach(kind, handle);

				if kind == ResourceKind::Orm {
					material.with_record(|record| record.set_metallic_roughness(1.0, 1.0));
				}
			}

			material.finalize();

			self.registry.push(&def.name, id.0);
		}

		log_info!(Asset; "Loaded {} materials from '{list_path}'", list.entries.len());

		true
	}
}
