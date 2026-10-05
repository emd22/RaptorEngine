use std::cell::RefCell;
use std::ffi::{CStr, CString, c_char, c_void};
use std::path::Path;

use raptor_asset::skin::build_skeleton;
use raptor_asset::{AlphaMode, GltfScene, Material, Primitive, TextureRef};

use crate::{LogFn, RxLogSink, RxSkeleton};

const LOG_INFO: i32 = 1;
const LOG_ERROR: i32 = 3;
const LOG_CATEGORY_ASSET: i32 = 5;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxGltfNode
{
	pub mesh: u32,
	pub skin: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxGltfPrimitive
{
	pub indices: *const u32,
	pub index_count: usize,
	pub positions: *const f32,
	pub position_floats: usize,
	pub normals: *const f32,
	pub normal_floats: usize,
	pub uvs: *const f32,
	pub uv_floats: usize,
	pub tangents: *const f32,
	pub tangent_floats: usize,
	pub weights: *const f32,
	pub weight_floats: usize,
	pub joints: *const u32,
	pub joint_values: usize,
	pub has_indices: u32,
	pub material: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxGltfTexture
{
	pub image: i32,
	pub source_image: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxGltfMaterial
{
	pub name: *const c_char,
	pub alpha_mode: u32,
	pub double_sided: u32,
	pub unlit: u32,
	pub has_specular_glossiness: u32,
	pub has_packed_occlusion: u32,
	pub packed_occlusion_strength: f32,
	pub diffuse_factor: [f32; 4],
	pub specular_factor: [f32; 3],
	pub glossiness_factor: f32,
	pub base_color_factor: [f32; 4],
	pub metallic_factor: f32,
	pub roughness_factor: f32,
	pub specular_glossiness_texture: RxGltfTexture,
	pub diffuse_texture: RxGltfTexture,
	pub metallic_roughness_texture: RxGltfTexture,
	pub base_color_texture: RxGltfTexture,
	pub normal_texture: RxGltfTexture,
}

#[repr(C)]
pub struct RxGltfImage
{
	pub name: *const c_char,
	pub bytes: *const u8,
	pub size: usize,
}

struct PrimitiveStore
{
	_data: Primitive,
	view: RxGltfPrimitive,
}

struct MaterialStore
{
	_name: Option<CString>,
	view: RxGltfMaterial,
}

pub struct RxGltf
{
	scene: GltfScene,
	nodes: Vec<RxGltfNode>,
	primitives: Vec<Vec<PrimitiveStore>>,
	materials: Vec<MaterialStore>,
	image_names: RefCell<Vec<Option<CString>>>,
	sink: Option<(usize, LogFn)>,
}

impl RxGltf
{
	fn log(&self, level: i32, message: &str)
	{
		let Some((user, log)) = self.sink else {
			return;
		};

		let text = CString::new(message.replace('\0', "")).unwrap_or_default();

		// SAFETY: the host promised `log` accepts a pointer and length pair.
		unsafe {
			log(
				user as *mut c_void,
				level,
				LOG_CATEGORY_ASSET,
				text.as_ptr(),
				text.as_bytes().len(),
			);
		}
	}
}

fn sink_of(log: *const RxLogSink) -> Option<(usize, LogFn)>
{
	// SAFETY: guaranteed by the caller of the exported functions.
	let sink = unsafe { log.as_ref() }?;

	sink.log.map(|log| (sink.user as usize, log))
}

fn index_or_none(index: Option<usize>) -> i32
{
	index.map_or(-1, |index| index as i32)
}

fn texture_to_c(texture: Option<TextureRef>) -> RxGltfTexture
{
	texture.map_or(
		RxGltfTexture {
			image: -1,
			source_image: -1,
		},
		|texture| RxGltfTexture {
			image: index_or_none(texture.image),
			source_image: index_or_none(texture.source_image),
		},
	)
}

fn primitive_store(data: Primitive) -> PrimitiveStore
{
	let view = RxGltfPrimitive {
		indices: data
			.indices
			.as_ref()
			.map_or(std::ptr::null(), |indices| indices.as_ptr()),
		index_count: data.indices.as_ref().map_or(0, Vec::len),
		positions: data.positions.as_ptr(),
		position_floats: data.positions.len(),
		normals: data.normals.as_ptr(),
		normal_floats: data.normals.len(),
		uvs: data.uvs.as_ptr(),
		uv_floats: data.uvs.len(),
		tangents: data.tangents.as_ptr(),
		tangent_floats: data.tangents.len(),
		weights: data.weights.as_ptr(),
		weight_floats: data.weights.len(),
		joints: data.joints.as_ptr(),
		joint_values: data.joints.len(),
		has_indices: u32::from(data.indices.is_some()),
		material: index_or_none(data.material),
	};

	PrimitiveStore { _data: data, view }
}

fn material_store(material: Material) -> MaterialStore
{
	let name = material
		.name
		.as_deref()
		.and_then(|name| CString::new(name.replace('\0', "")).ok());

	let view = RxGltfMaterial {
		name: name.as_ref().map_or(std::ptr::null(), |name| name.as_ptr()),
		alpha_mode: match material.alpha_mode {
			AlphaMode::Opaque => 0,
			AlphaMode::Mask => 1,
			AlphaMode::Blend => 2,
		},
		double_sided: u32::from(material.double_sided),
		unlit: u32::from(material.unlit),
		has_specular_glossiness: u32::from(material.has_specular_glossiness),
		has_packed_occlusion: u32::from(material.packed_occlusion_strength.is_some()),
		packed_occlusion_strength: material.packed_occlusion_strength.unwrap_or_default(),
		diffuse_factor: material.diffuse_factor,
		specular_factor: material.specular_factor,
		glossiness_factor: material.glossiness_factor,
		base_color_factor: material.base_color_factor,
		metallic_factor: material.metallic_factor,
		roughness_factor: material.roughness_factor,
		specular_glossiness_texture: texture_to_c(material.specular_glossiness_texture),
		diffuse_texture: texture_to_c(material.diffuse_texture),
		metallic_roughness_texture: texture_to_c(material.metallic_roughness_texture),
		base_color_texture: texture_to_c(material.base_color_texture),
		normal_texture: texture_to_c(material.normal_texture),
	};

	MaterialStore { _name: name, view }
}

fn into_handle(scene: GltfScene, sink: Option<(usize, LogFn)>) -> *mut RxGltf
{
	let nodes = scene
		.mesh_nodes()
		.iter()
		.map(|node| RxGltfNode {
			mesh: node.mesh as u32,
			skin: index_or_none(node.skin),
		})
		.collect();

	let primitives = (0..scene.mesh_count())
		.map(|mesh| {
			(0..scene.primitive_count(mesh))
				.filter_map(|primitive| scene.primitive(mesh, primitive))
				.map(primitive_store)
				.collect()
		})
		.collect();

	let materials = (0..scene.material_count())
		.filter_map(|index| scene.material(index))
		.map(material_store)
		.collect();

	let image_names = RefCell::new(vec![None; scene.document().images().len()]);

	Box::into_raw(Box::new(RxGltf {
		scene,
		nodes,
		primitives,
		materials,
		image_names,
		sink,
	}))
}

fn report_error(sink: Option<(usize, LogFn)>, message: &str)
{
	let Some((user, log)) = sink else {
		return;
	};

	let text = CString::new(message.replace('\0', "")).unwrap_or_default();

	// SAFETY: the host promised `log` accepts a pointer and length pair.
	unsafe {
		log(
			user as *mut c_void,
			LOG_ERROR,
			LOG_CATEGORY_ASSET,
			text.as_ptr(),
			text.as_bytes().len(),
		);
	}
}

/// Reads a glTF or GLB file and the files it refers to. Returns null, after saying why through `log`,
/// if it could not be read.
///
/// # Safety
///
/// `path` must be NUL-terminated and `log` null or valid for as long as the result lives.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_load_file(path: *const c_char, log: *const RxLogSink) -> *mut RxGltf
{
	let sink = sink_of(log);

	// SAFETY: guaranteed by the caller.
	let path = unsafe { CStr::from_ptr(path) }.to_string_lossy().into_owned();

	match GltfScene::open(Path::new(&path)) {
		Ok(scene) => into_handle(scene, sink),
		Err(error) => {
			report_error(sink, &error.to_string());
			std::ptr::null_mut()
		}
	}
}

/// Reads a glTF or GLB that is in memory. Files it refers to are not read.
///
/// # Safety
///
/// `data` must point at `size` readable bytes, and `log` be null or valid for as long as the result
/// lives.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_load_memory(
	data: *const u8,
	size: usize,
	log: *const RxLogSink,
) -> *mut RxGltf
{
	let sink = sink_of(log);

	let data = if data.is_null() {
		&[][..]
	}
	else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(data, size) }
	};

	match GltfScene::from_bytes(data, None) {
		Ok(scene) => into_handle(scene, sink),
		Err(error) => {
			report_error(sink, &error.to_string());
			std::ptr::null_mut()
		}
	}
}

/// # Safety
///
/// `gltf` must be null or come from a load function, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_free(gltf: *mut RxGltf)
{
	if !gltf.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(gltf) });
	}
}

/// # Safety
///
/// `gltf` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_node_count(gltf: *const RxGltf) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*gltf }.nodes.len() as u32
}

/// The nodes that draw a mesh, in the order of the file.
///
/// # Safety
///
/// `gltf` must be live. The node lasts as long as the scene.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_node(gltf: *const RxGltf, index: u32) -> *const RxGltfNode
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*gltf }
		.nodes
		.get(index as usize)
		.map_or(std::ptr::null(), std::ptr::from_ref)
}

/// # Safety
///
/// `gltf` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_mesh_count(gltf: *const RxGltf) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*gltf }.scene.mesh_count() as u32
}

/// # Safety
///
/// `gltf` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_skin_count(gltf: *const RxGltf) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*gltf }.scene.skin_count() as u32
}

/// # Safety
///
/// `gltf` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_primitive_count(gltf: *const RxGltf, mesh: u32) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*gltf }
		.primitives
		.get(mesh as usize)
		.map_or(0, |primitives| primitives.len() as u32)
}

/// # Safety
///
/// `gltf` must be live. The primitive and the arrays it points at last as long as the scene.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_primitive(
	gltf: *const RxGltf,
	mesh: u32,
	primitive: u32,
) -> *const RxGltfPrimitive
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*gltf }
		.primitives
		.get(mesh as usize)
		.and_then(|primitives| primitives.get(primitive as usize))
		.map_or(std::ptr::null(), |store| &raw const store.view)
}

/// # Safety
///
/// `gltf` must be live. The material lasts as long as the scene.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_material(gltf: *const RxGltf, index: u32) -> *const RxGltfMaterial
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*gltf }
		.materials
		.get(index as usize)
		.map_or(std::ptr::null(), |store| &raw const store.view)
}

/// The bytes of an image. Returns 0 if there are none, such as for a file that is missing.
///
/// # Safety
///
/// `gltf` must be live and `out` writable. What it points at lasts as long as the scene.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_image(gltf: *const RxGltf, index: u32, out: *mut RxGltfImage) -> u8
{
	// SAFETY: guaranteed by the caller.
	let gltf = unsafe { &*gltf };

	let Some(blob) = gltf.scene.image(index as usize) else {
		return 0;
	};

	let mut names = gltf.image_names.borrow_mut();

	let Some(slot) = names.get_mut(index as usize) else {
		return 0;
	};

	let name = slot.get_or_insert_with(|| CString::new(blob.name.replace('\0', "")).unwrap_or_default());

	// SAFETY: guaranteed by the caller.
	unsafe {
		out.write(RxGltfImage {
			name: name.as_ptr(),
			bytes: blob.bytes.as_ptr(),
			size: blob.bytes.len(),
		});
	}

	1
}

/// The name of an image for messages, whether or not its bytes could be read. Null if there is no
/// such image.
///
/// # Safety
///
/// `gltf` must be live. The name lasts as long as the scene.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_image_name(gltf: *const RxGltf, index: u32) -> *const c_char
{
	// SAFETY: guaranteed by the caller.
	let gltf = unsafe { &*gltf };

	let mut names = gltf.image_names.borrow_mut();

	let Some(slot) = names.get_mut(index as usize) else {
		return std::ptr::null();
	};

	if slot.is_none() {
		let image = gltf.scene.document().images().nth(index as usize);

		let name = image
			.as_ref()
			.and_then(|image| image.name().map(str::to_owned))
			.or_else(|| match image.as_ref()?.source() {
				gltf::image::Source::Uri { uri, .. } if !uri.starts_with("data:") => Some(uri.to_owned()),
				_ => None,
			})
			.unwrap_or_else(|| "<unnamed>".to_owned());

		*slot = CString::new(name.replace('\0', "")).ok();
	}

	slot.as_ref().map_or(std::ptr::null(), |name| name.as_ptr())
}

/// Makes the skeleton of a skin, with every animation in the file. Returns null if there is no such
/// skin.
///
/// # Safety
///
/// `gltf` must be live. The skeleton is the caller's, to free with `rx_skeleton_free`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_gltf_skeleton_new(gltf: *const RxGltf, skin: u32) -> *mut RxSkeleton
{
	// SAFETY: guaranteed by the caller.
	let gltf = unsafe { &*gltf };

	let Some(skeleton) = build_skeleton(&gltf.scene, skin as usize) else {
		return std::ptr::null_mut();
	};

	let joints = skeleton.joint_count();

	for animation in skeleton.data().animations() {
		gltf.log(
			LOG_INFO,
			&format!(
				"Loaded animation '{}': {:.3}s, {joints} joints",
				animation.name, animation.duration
			),
		);
	}

	let count = skeleton.data().animations().len();

	if count > 0 {
		gltf.log(LOG_INFO, &format!("Loaded {count} animations"));
	}

	Box::into_raw(Box::new(skeleton)).cast()
}

const _: () = {
	use std::mem::size_of;

	assert!(size_of::<RxGltfNode>() == 8);
	assert!(size_of::<RxGltfPrimitive>() == 120);
	assert!(size_of::<RxGltfTexture>() == 8);
	assert!(size_of::<RxGltfMaterial>() == 128);
	assert!(size_of::<RxGltfImage>() == 24);
};
