use std::cell::RefCell;
use std::path::{Path, PathBuf};

use gltf::Gltf;
use gltf::accessor::Iter;

#[derive(Debug, PartialEq, Eq)]
pub enum LoadError
{
	/// The file could not be read
	Unreadable(String),
	/// The text is not a glTF document
	NotGltf(String),
	/// A buffer the document refers to could not be loaded
	Buffer(String),
}

impl std::fmt::Display for LoadError
{
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result
	{
		match self {
			Self::Unreadable(message) | Self::NotGltf(message) | Self::Buffer(message) => {
				f.write_str(message)
			}
		}
	}
}

/// A node that draws a mesh
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SceneNode
{
	pub mesh: usize,
	pub skin: Option<usize>,
}

/// The vertices of one primitive, unpacked into flat arrays of floats. Anything the primitive does not
/// have is left empty.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Primitive
{
	pub indices: Option<Vec<u32>>,
	pub positions: Vec<f32>,
	pub normals: Vec<f32>,
	pub uvs: Vec<f32>,
	pub tangents: Vec<f32>,
	pub weights: Vec<f32>,
	/// Four joints for each vertex
	pub joints: Vec<u32>,
	pub material: Option<usize>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AlphaMode
{
	Opaque,
	Mask,
	Blend,
}

/// Which image a texture reads, with the KTX2 image from `KHR_texture_basisu` taking priority over
/// the regular one
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TextureRef
{
	pub image: Option<usize>,
	/// The image the texture reads without the extension, which is what decides whether two textures
	/// share an image
	pub source_image: Option<usize>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Material
{
	pub name: Option<String>,
	pub alpha_mode: AlphaMode,
	pub double_sided: bool,
	pub unlit: bool,

	pub has_specular_glossiness: bool,
	pub diffuse_factor: [f32; 4],
	pub specular_factor: [f32; 3],
	pub glossiness_factor: f32,
	pub specular_glossiness_texture: Option<TextureRef>,
	pub diffuse_texture: Option<TextureRef>,

	/// The glTF defaults apply to a material with no metallic-roughness block
	pub base_color_factor: [f32; 4],
	pub metallic_factor: f32,
	pub roughness_factor: f32,
	pub metallic_roughness_texture: Option<TextureRef>,
	pub base_color_texture: Option<TextureRef>,

	pub normal_texture: Option<TextureRef>,
	/// The occlusion strength, if occlusion shares its image with the metallic-roughness texture, as
	/// it does when the two are packed into one image
	pub packed_occlusion_strength: Option<f32>,
}

pub struct ImageBlob
{
	pub name: String,
	pub bytes: Vec<u8>,
}

pub struct GltfScene
{
	document: gltf::Document,
	buffers: Vec<Vec<u8>>,
	base_directory: Option<PathBuf>,
	images: RefCell<Vec<Option<Option<std::rc::Rc<ImageBlob>>>>>,
}

fn percent_decode(text: &str) -> String
{
	let bytes = text.as_bytes();
	let mut out = Vec::with_capacity(bytes.len());
	let mut index = 0;

	while index < bytes.len() {
		if bytes[index] == b'%' && index + 2 < bytes.len() {
			let hex = std::str::from_utf8(&bytes[index + 1..index + 3]).ok();

			if let Some(value) = hex.and_then(|hex| u8::from_str_radix(hex, 16).ok()) {
				out.push(value);
				index += 3;
				continue;
			}
		}

		out.push(bytes[index]);
		index += 1;
	}

	String::from_utf8_lossy(&out).into_owned()
}

/// The bytes of a `data:` URI with base64 content
fn decode_data_uri(uri: &str) -> Option<Vec<u8>>
{
	use base64::Engine;

	let rest = uri.strip_prefix("data:")?;
	let comma = rest.find(',')?;

	if !rest[..comma].ends_with(";base64") {
		return None;
	}

	base64::engine::general_purpose::STANDARD
		.decode(rest[comma + 1..].trim_end())
		.ok()
}

fn load_buffers(
	document: &gltf::Document,
	blob: Option<Vec<u8>>,
	base_directory: Option<&Path>,
) -> Result<Vec<Vec<u8>>, LoadError>
{
	let mut blob = blob;
	let mut buffers = Vec::new();

	for buffer in document.buffers() {
		let data = match buffer.source() {
			gltf::buffer::Source::Bin => blob.take().ok_or_else(|| {
				LoadError::Buffer("A buffer refers to the binary chunk of a file that has none".to_owned())
			})?,
			gltf::buffer::Source::Uri(uri) => {
				if uri.starts_with("data:") {
					decode_data_uri(uri).ok_or_else(|| {
						LoadError::Buffer("A buffer holds data that is not base64".to_owned())
					})?
				}
				else {
					let directory = base_directory.ok_or_else(|| {
						LoadError::Buffer(format!("The buffer '{uri}' is a file, and there is no directory to look in"))
					})?;

					std::fs::read(directory.join(percent_decode(uri)))
						.map_err(|error| LoadError::Buffer(format!("Could not read the buffer '{uri}': {error}")))?
				}
			}
		};

		buffers.push(data);
	}

	Ok(buffers)
}

fn texture_ref(texture: &gltf::Texture) -> TextureRef
{
	let source_image = texture.source().map(|image| image.index());

	let basisu = texture
		.extension_value("KHR_texture_basisu")
		.and_then(|value| value.get("source"))
		.and_then(|source| source.as_u64())
		.map(|index| index as usize);

	TextureRef {
		image: basisu.or(source_image),
		source_image,
	}
}

fn material_data(material: &gltf::Material) -> Material
{
	let pbr = material.pbr_metallic_roughness();

	let specular = material.pbr_specular_glossiness();

	let view = |info: Option<gltf::texture::Info>| info.map(|info| texture_ref(&info.texture()));

	let metallic_roughness_texture = view(pbr.metallic_roughness_texture());

	let occlusion = material.occlusion_texture();

	let packed_occlusion_strength = match (&occlusion, &metallic_roughness_texture, &specular) {
		(Some(occlusion), Some(metallic_roughness), None) => {
			let occlusion_image = texture_ref(&occlusion.texture()).source_image;

			(occlusion_image == metallic_roughness.source_image).then(|| occlusion.strength())
		}
		_ => None,
	};

	Material {
		name: material.name().map(str::to_owned),
		alpha_mode: match material.alpha_mode() {
			gltf::material::AlphaMode::Opaque => AlphaMode::Opaque,
			gltf::material::AlphaMode::Mask => AlphaMode::Mask,
			gltf::material::AlphaMode::Blend => AlphaMode::Blend,
		},
		double_sided: material.double_sided(),
		unlit: material.unlit(),

		has_specular_glossiness: specular.is_some(),
		diffuse_factor: specular.as_ref().map_or([0.0; 4], |sg| sg.diffuse_factor()),
		specular_factor: specular.as_ref().map_or([0.0; 3], |sg| sg.specular_factor()),
		glossiness_factor: specular.as_ref().map_or(0.0, |sg| sg.glossiness_factor()),
		specular_glossiness_texture: specular
			.as_ref()
			.and_then(|sg| view(sg.specular_glossiness_texture())),
		diffuse_texture: specular.as_ref().and_then(|sg| view(sg.diffuse_texture())),

		base_color_factor: pbr.base_color_factor(),
		metallic_factor: pbr.metallic_factor(),
		roughness_factor: pbr.roughness_factor(),
		metallic_roughness_texture,
		base_color_texture: view(pbr.base_color_texture()),

		normal_texture: material
			.normal_texture()
			.map(|normal| texture_ref(&normal.texture())),
		packed_occlusion_strength,
	}
}

fn collect<T: gltf::accessor::Item>(iter: Option<Iter<T>>) -> Vec<T>
{
	iter.map(Iterator::collect).unwrap_or_default()
}

fn flatten<const N: usize>(items: Vec<[f32; N]>) -> Vec<f32>
{
	items.into_iter().flatten().collect()
}

impl GltfScene
{
	/// Reads a glTF or GLB file and the buffers it refers to
	pub fn open(path: &Path) -> Result<Self, LoadError>
	{
		let data = std::fs::read(path)
			.map_err(|error| LoadError::Unreadable(format!("Could not read '{}': {error}", path.display())))?;

		Self::from_bytes(&data, path.parent().or(Some(Path::new(""))))
	}

	/// Reads a glTF or GLB held in memory. `base_directory` is where the files it refers to are, if
	/// there are any.
	pub fn from_bytes(data: &[u8], base_directory: Option<&Path>) -> Result<Self, LoadError>
	{
		let Gltf { document, blob } = Gltf::from_slice_without_validation(data)
			.map_err(|error| LoadError::NotGltf(error.to_string()))?;

		let buffers = load_buffers(&document, blob, base_directory)?;

		let image_count = document.images().len();

		Ok(Self {
			document,
			buffers,
			base_directory: base_directory.map(Path::to_path_buf),
			images: RefCell::new(vec![None; image_count]),
		})
	}

	pub fn document(&self) -> &gltf::Document
	{
		&self.document
	}

	pub fn buffers(&self) -> &[Vec<u8>]
	{
		&self.buffers
	}

	pub fn mesh_count(&self) -> usize
	{
		self.document.meshes().len()
	}

	pub fn skin_count(&self) -> usize
	{
		self.document.skins().len()
	}

	/// The nodes that draw a mesh, in the order the file lists its nodes
	pub fn mesh_nodes(&self) -> Vec<SceneNode>
	{
		self.document
			.nodes()
			.filter_map(|node| {
				node.mesh().map(|mesh| SceneNode {
					mesh: mesh.index(),
					skin: node.skin().map(|skin| skin.index()),
				})
			})
			.collect()
	}

	pub fn primitive_count(&self, mesh: usize) -> usize
	{
		self.document
			.meshes()
			.nth(mesh)
			.map_or(0, |mesh| mesh.primitives().len())
	}

	pub fn primitive(&self, mesh: usize, primitive: usize) -> Option<Primitive>
	{
		let primitive = self.document.meshes().nth(mesh)?.primitives().nth(primitive)?;

		let reader = primitive.reader(|buffer| self.buffers.get(buffer.index()).map(Vec::as_slice));

		Some(Primitive {
			indices: reader.read_indices().map(|indices| indices.into_u32().collect()),
			positions: flatten(collect(reader.read_positions())),
			normals: flatten(collect(reader.read_normals())),
			uvs: reader
				.read_tex_coords(0)
				.map(|uvs| flatten(uvs.into_f32().collect()))
				.unwrap_or_default(),
			tangents: flatten(collect(reader.read_tangents())),
			weights: reader
				.read_weights(0)
				.map(|weights| flatten(weights.into_f32().collect()))
				.unwrap_or_default(),
			joints: reader
				.read_joints(0)
				.map(|joints| {
					joints
						.into_u16()
						.flat_map(|joint| joint.map(u32::from))
						.collect()
				})
				.unwrap_or_default(),
			material: primitive.material().index(),
		})
	}

	pub fn material_count(&self) -> usize
	{
		self.document.materials().len()
	}

	pub fn material(&self, index: usize) -> Option<Material>
	{
		self.document
			.materials()
			.nth(index)
			.map(|material| material_data(&material))
	}

	/// The bytes of an image, whether they are in a buffer view, in a `data:` URI or in a file
	/// beside the model. None if there are none or the file could not be read.
	pub fn image(&self, index: usize) -> Option<std::rc::Rc<ImageBlob>>
	{
		if let Some(Some(cached)) = self.images.borrow().get(index) {
			return cached.clone();
		}

		let image = self.document.images().nth(index)?;

		let name = image
			.name()
			.map(str::to_owned)
			.or_else(|| match image.source() {
				gltf::image::Source::Uri { uri, .. } if !uri.starts_with("data:") => Some(uri.to_owned()),
				_ => None,
			})
			.unwrap_or_else(|| "<unnamed>".to_owned());

		let bytes = match image.source() {
			gltf::image::Source::View { view, .. } => {
				let buffer = self.buffers.get(view.buffer().index())?;

				buffer
					.get(view.offset()..view.offset() + view.length())
					.map(<[u8]>::to_vec)
			}
			gltf::image::Source::Uri { uri, .. } => {
				if uri.starts_with("data:") {
					decode_data_uri(uri)
				}
				else {
					self.base_directory
						.as_ref()
						.and_then(|directory| std::fs::read(directory.join(percent_decode(uri))).ok())
				}
			}
		};

		let blob = bytes.map(|bytes| std::rc::Rc::new(ImageBlob { name, bytes }));

		self.images.borrow_mut()[index] = Some(blob.clone());

		blob
	}
}
