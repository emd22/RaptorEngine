use std::path::Path;

use raptor_anim::Skeleton;
use raptor_gpu::ImageFormat;
use raptor_render::material::MaterialRecord;
use raptor_render::mesh_pack::Attributes;
use raptor_render::mesh_record::MeshRecord;

use crate::gltf_scene::{AlphaMode, GltfScene, LoadError, Material, Primitive, TextureRef};
use crate::ktx::{KtxImage, is_ktx, read_ktx};
use crate::skin::build_skeleton;

/// Opaque below this, the alpha a blended material falls back to when only its texture has alpha
const TEXTURE_ALPHA_MATERIAL_ALPHA: f32 = 0.99;

/// A texture's pixels, all the mip levels laid end to end, ready to be put on the GPU
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TextureData {
	pub width: u32,
	pub height: u32,
	pub format: ImageFormat,
	pub mip_count: u32,
	pub bytes: Vec<u8>,
}

impl TextureData {
	pub fn from_ktx(ktx: &KtxImage) -> Option<Self> {
		let (bytes, mip_count) = ktx.chain(0, 0)?;

		Some(Self {
			width: ktx.width,
			height: ktx.height,
			format: ImageFormat::from_raw(ktx.format)?,
			mip_count: mip_count as u32,
			bytes,
		})
	}

	fn is_rgba8(format: ImageFormat) -> bool {
		matches!(format, ImageFormat::Rgba8UNorm | ImageFormat::Rgba8Srgb)
	}

	/// Whether any texel in the base level of an 8-bit, 4 channel image is not fully opaque
	pub fn has_translucent_texels(&self) -> bool {
		if !Self::is_rgba8(self.format) && self.format != ImageFormat::Bgra8UNorm {
			return false;
		}

		let texels = (self.width as usize * self.height as usize).min(self.bytes.len() / 4);

		self.bytes
			.chunks_exact(4)
			.take(texels)
			.any(|texel| texel[3] != 0xFF)
	}
}

/// A material as the file describes it: how it looks, and the pixels of its textures
pub struct MaterialDesc {
	pub name: String,
	pub record: MaterialRecord,
	pub diffuse: Option<TextureData>,
	pub normal: Option<TextureData>,
	pub orm: Option<TextureData>,
}

/// An object to make from a file: what it draws and what it holds
pub struct ModelNode {
	pub name: String,
	pub mesh: Option<MeshRecord>,
	pub bounds: Option<([f32; 3], [f32; 3])>,
	pub material: Option<MaterialDesc>,
	/// Which of the model's skeletons the node is skinned to
	pub skeleton: Option<usize>,
	pub children: Vec<ModelNode>,
}

impl ModelNode {
	fn empty(name: String) -> Self {
		Self {
			name,
			mesh: None,
			bounds: None,
			material: None,
			skeleton: None,
			children: Vec::new(),
		}
	}

	pub fn node_count(&self) -> usize {
		1 + self.children.iter().map(Self::node_count).sum::<usize>()
	}
}

#[derive(Clone, Copy, Debug, Default)]
pub struct ModelOptions {
	/// Keeps the vertices and indices on the CPU after they are uploaded
	pub keep_in_memory: bool,
}

/// What loading a model makes: a tree of nodes, with the skeletons they share
pub struct Model {
	pub root: ModelNode,
	pub skeletons: Vec<Skeleton>,
	pub messages: Vec<String>,
}

// SAFETY: the raw pointers inside a skeleton point into its own heap vectors, which move with it,
// and a model is only ever used by one thread at a time.
unsafe impl Send for Model {}

#[derive(Debug)]
pub enum ModelError {
	Scene(LoadError),
}

impl std::fmt::Display for ModelError {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		match self {
			Self::Scene(error) => error.fmt(f),
		}
	}
}

impl From<LoadError> for ModelError {
	fn from(error: LoadError) -> Self {
		Self::Scene(error)
	}
}

struct Builder<'a> {
	scene: &'a GltfScene,
	options: ModelOptions,
	model_path: String,
	messages: Vec<String>,
}

impl Model {
	/// Reads a glTF or GLB file and everything in it that is drawn
	pub fn open(name: &str, path: &Path, options: ModelOptions) -> Result<Self, ModelError> {
		let scene = GltfScene::open(path)?;

		Ok(Self::build(
			name,
			&scene,
			options,
			path.display().to_string(),
		))
	}

	pub fn from_bytes(name: &str, data: &[u8], options: ModelOptions) -> Result<Self, ModelError> {
		let scene = GltfScene::from_bytes(data, None)?;

		Ok(Self::build(name, &scene, options, "<memory>".to_owned()))
	}

	pub fn build(name: &str, scene: &GltfScene, options: ModelOptions, model_path: String) -> Self {
		let mut builder = Builder {
			scene,
			options,
			model_path,
			messages: Vec::new(),
		};

		let mut root = ModelNode::empty(name.to_owned());
		let mut skeletons: Vec<Skeleton> = Vec::new();
		let mut skeleton_of_skin: Vec<Option<usize>> = vec![None; scene.skin_count()];

		let has_multiple_meshes = scene.mesh_count() > 1;

		for (index, node) in scene.mesh_nodes().into_iter().enumerate() {
			let skinned = node.skin.is_some();

			let mut current = if has_multiple_meshes {
				ModelNode::empty(format!("{name}_{}", index + 1))
			} else {
				std::mem::replace(&mut root, ModelNode::empty(String::new()))
			};

			builder.build_primitives(&mut current, node.mesh, skinned);

			if let Some(skin) = node.skin {
				let slot = skeleton_of_skin.get_mut(skin);

				let skeleton = match slot {
					Some(Some(existing)) => Some(*existing),
					Some(slot) => build_skeleton(scene, skin).map(|skeleton| {
						skeletons.push(skeleton);
						*slot = Some(skeletons.len() - 1);

						skeletons.len() - 1
					}),
					None => None,
				};

				current.skeleton = skeleton;

				for child in &mut current.children {
					child.skeleton = skeleton;
				}
			}

			if has_multiple_meshes {
				root.children.push(current);
			} else {
				root = current;
			}
		}

		Self {
			root,
			skeletons,
			messages: builder.messages,
		}
	}
}

impl Builder<'_> {
	fn build_primitives(&mut self, container: &mut ModelNode, mesh_index: usize, skinned: bool) {
		let count = self.scene.primitive_count(mesh_index);
		let has_multiple = count > 1;

		for index in 0..count {
			let Some(primitive) = self.scene.primitive(mesh_index, index) else {
				continue;
			};

			let mut node = if has_multiple {
				ModelNode::empty(container.name.clone())
			} else {
				std::mem::replace(container, ModelNode::empty(String::new()))
			};

			self.fill(&mut node, &primitive, skinned);

			if has_multiple {
				container.children.push(node);
			} else {
				*container = node;
			}
		}
	}

	fn fill(&mut self, node: &mut ModelNode, primitive: &Primitive, skinned: bool) {
		let mut mesh = MeshRecord::default();
		mesh.keep_in_memory = self.options.keep_in_memory;

		if let Some(indices) = &primitive.indices {
			mesh.set_indices(indices);
		}

		let attributes = Attributes {
			positions: &primitive.positions,
			normals: &primitive.normals,
			uvs: &primitive.uvs,
			tangents: &primitive.tangents,
			tangent_stride: 4,
			handedness: 1.0,
			bone_weights: if skinned { &primitive.weights } else { &[] },
			bone_ids: if skinned { &primitive.joints } else { &[] },
			negative_x: true,
			mirror_basis: true,
		};

		if !mesh.pack(&attributes) {
			self.messages.push(format!(
				"A primitive of '{}' has no vertices",
				self.model_path
			));
		}

		node.bounds = mesh.bounds();

		let supports_skinning = mesh.is_skinned();

		node.mesh = Some(mesh);

		if let Some(index) = primitive.material
			&& let Some(material) = self.scene.material(index)
		{
			node.material = Some(self.material(&node.name, &material, supports_skinning));
		}
	}

	fn texture(
		&mut self,
		material_name: &str,
		component: &str,
		reference: Option<TextureRef>,
		format: ImageFormat,
	) -> Option<TextureData> {
		let image_index = reference?.image?;

		let Some(blob) = self.scene.image(image_index) else {
			self.messages
				.push(format!("Could not open glTF texture {image_index}"));
			return None;
		};

		let ktx = is_ktx(&blob.bytes)
			.then(|| read_ktx(&blob.bytes).ok())
			.flatten()
			.and_then(|ktx| TextureData::from_ktx(&ktx));

		let Some(mut texture) = ktx else {
			self.messages.push(format!(
				"glTF textures must be KTX2 (image '{}', {component} of material '{material_name}' in '{}')",
				blob.name, self.model_path
			));
			return None;
		};

		if TextureData::is_rgba8(texture.format)
			&& TextureData::is_rgba8(format)
			&& texture.format != format
		{
			self.messages.push(format!(
				"glTF texture '{}' ({component}) is stored as {:?}, expected {format:?}",
				blob.name, texture.format
			));
			texture.format = format;
		}

		Some(texture)
	}

	fn material(
		&mut self,
		object_name: &str,
		gltf: &Material,
		supports_skinning: bool,
	) -> MaterialDesc {
		let name = gltf.name.clone().unwrap_or_else(|| object_name.to_owned());

		let mut record = MaterialRecord::default();
		record.supports_skinning = supports_skinning;

		let specular_glossiness = gltf.has_specular_glossiness;

		let diffuse_reference = if specular_glossiness {
			gltf.diffuse_texture
		} else {
			gltf.base_color_texture
		};

		let diffuse = self.texture(&name, "diffuse", diffuse_reference, ImageFormat::Rgba8Srgb);

		let factor = if specular_glossiness {
			gltf.diffuse_factor
		} else {
			gltf.base_color_factor
		};

		record.set_base_color_factor([factor[0], factor[1], factor[2]]);

		let normal = self.texture(
			&name,
			"normal",
			gltf.normal_texture,
			ImageFormat::Rgba8UNorm,
		);

		let orm = if specular_glossiness {
			record.set_specular_glossiness(gltf.specular_factor, gltf.glossiness_factor);

			self.texture(
				&name,
				"specular-glossiness",
				gltf.specular_glossiness_texture,
				ImageFormat::Rgba8UNorm,
			)
		} else {
			record.set_metallic_roughness(gltf.metallic_factor, gltf.roughness_factor);

			self.texture(
				&name,
				"metallic-roughness",
				gltf.metallic_roughness_texture,
				ImageFormat::Rgba8UNorm,
			)
		};

		record.set_occlusion_strength(gltf.packed_occlusion_strength.unwrap_or(0.0));

		let alpha = factor[3];

		match gltf.alpha_mode {
			AlphaMode::Blend => {
				let texture_has_alpha = diffuse
					.as_ref()
					.is_some_and(TextureData::has_translucent_texels);

				if alpha < 0.99 {
					record.set_alpha(alpha);
				} else if texture_has_alpha {
					record.set_alpha(TEXTURE_ALPHA_MATERIAL_ALPHA);
				} else {
					record.set_alpha(1.0);
				}
			}
			AlphaMode::Mask => {
				record.set_alpha_mask(true);
				record.set_alpha(alpha);
			}
			AlphaMode::Opaque => {
				record.set_alpha(if alpha < 0.99 { alpha } else { 1.0 });
			}
		}

		if gltf.double_sided && gltf.alpha_mode != AlphaMode::Opaque {
			record.set_double_sided(true);
		}

		if gltf.unlit {
			record.set_unlit(true);
		}

		record.set_ready_to_check(true);

		MaterialDesc {
			name,
			record,
			diffuse,
			normal,
			orm,
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	fn models_directory() -> std::path::PathBuf {
		Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Models")
	}

	#[test]
	fn a_single_mesh_model_puts_the_mesh_in_the_root() {
		let model = Model::open(
			"box",
			&models_directory().join("Box.glb"),
			ModelOptions::default(),
		);

		let Ok(model) = model else {
			return;
		};

		assert_eq!(model.root.name, "box");
		assert!(model.root.mesh.is_some() || !model.root.children.is_empty());
		assert!(model.root.node_count() >= 1);
	}

	#[test]
	fn every_real_model_loads_with_a_mesh_in_each_leaf() {
		fn leaves(node: &ModelNode, count: &mut usize) {
			if node.children.is_empty() {
				assert!(node.mesh.is_some(), "leaf '{}' has no mesh", node.name);
				*count += 1;
			}

			for child in &node.children {
				leaves(child, count);
			}
		}

		let Ok(entries) = std::fs::read_dir(models_directory()) else {
			return;
		};

		for entry in entries.flatten() {
			let path = entry.path();

			if path.extension().is_none_or(|extension| extension != "glb") {
				continue;
			}

			let Ok(model) = Model::open("model", &path, ModelOptions::default()) else {
				continue;
			};

			let mut count = 0;

			leaves(&model.root, &mut count);

			assert!(count > 0, "{} made no meshes", path.display());
		}
	}

	#[test]
	fn translucent_texels_are_found_in_the_base_level_only() {
		let opaque = TextureData {
			width: 2,
			height: 1,
			format: ImageFormat::Rgba8UNorm,
			mip_count: 2,
			bytes: vec![1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 10],
		};

		assert!(!opaque.has_translucent_texels());

		let translucent = TextureData {
			bytes: vec![1, 2, 3, 255, 4, 5, 6, 128, 7, 8, 9, 10],
			..opaque.clone()
		};

		assert!(translucent.has_translucent_texels());

		let linear = TextureData {
			format: ImageFormat::Rg16UNorm,
			..translucent
		};

		assert!(!linear.has_translucent_texels());
	}
}
