use std::sync::{Arc, Mutex, MutexGuard};

use ash::vk;
use raptor_core::{log_error, log_info};
use raptor_gfx::{Gfx, GpuBuffer};
use raptor_gpu::{
	BufferRecord, BufferType, DescriptorEntryRef, DescriptorResourceRef, Filter, ImageFormat,
	Memory, SamplerProps,
};
use raptor_render::material::{ComponentState, FLAG_DOUBLE_SIDED, MaterialRecord};

use crate::manager::{AssetManager, ImageHandle};
use crate::model::{MaterialDesc, TextureData};

pub const MAX_MATERIALS: u32 = 64;
const ANISOTROPY: u8 = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct MaterialId(pub u32);

impl MaterialId {
	pub const NULL: MaterialId = MaterialId(0);

	pub fn is_null(self) -> bool {
		self.0 == 0
	}
}

impl std::fmt::Display for MaterialId {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		write!(f, "MaterialID({})", self.0)
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ResourceKind {
	Diffuse,
	Normal,
	Orm,
}

struct Component {
	format: ImageFormat,
	image: Option<ImageHandle>,
}

impl Component {
	fn new(format: ImageFormat) -> Self {
		Self {
			format,
			image: None,
		}
	}

	fn state(&self) -> ComponentState {
		ComponentState {
			exists: self.image.is_some(),
			has_image: self.image.is_some(),
			loaded: self.image.as_ref().is_some_and(ImageHandle::is_loaded),
		}
	}

	fn max_lod(&self) -> f32 {
		self.image
			.as_ref()
			.map_or(0.0, |handle| handle.image.mip_count() as f32)
	}

	fn min_lod(&self) -> f32 {
		self.image
			.as_ref()
			.map_or(0.0, |handle| handle.image.mip_level() as f32)
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ComponentStatus {
	Ready,
	Missing,
	NotReady,
}

impl Component {
	fn status(&self) -> ComponentStatus {
		match &self.image {
			None => ComponentStatus::Missing,
			Some(handle) if handle.is_loaded() => ComponentStatus::Ready,
			Some(_) => ComponentStatus::NotReady,
		}
	}
}

struct State {
	record: MaterialRecord,
	diffuse: Component,
	normal: Component,
	orm: Component,
	descriptor_set: Option<u32>,
}

/// A material: its properties, its textures and the descriptor set that binds them
pub struct Material {
	pub name: String,
	state: Mutex<State>,
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
	mutex
		.lock()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
}

impl Material {
	fn new(name: &str, record: MaterialRecord) -> Self {
		Self {
			name: name.to_owned(),
			state: Mutex::new(State {
				record,
				diffuse: Component::new(ImageFormat::Rgba8Srgb),
				normal: Component::new(ImageFormat::Rgba8UNorm),
				orm: Component::new(ImageFormat::Rgba8UNorm),
				descriptor_set: None,
			}),
		}
	}

	pub fn id(&self) -> MaterialId {
		MaterialId(lock(&self.state).record.id)
	}

	/// Reads or changes the material's properties
	pub fn with_record<R>(&self, f: impl FnOnce(&mut MaterialRecord) -> R) -> R {
		f(&mut lock(&self.state).record)
	}

	pub fn attach(&self, kind: ResourceKind, image: ImageHandle) {
		let mut state = lock(&self.state);

		let level = image.image.mip_level() as i32;
		state.record.quality_level = state.record.quality_level.min(level);

		let component = match kind {
			ResourceKind::Diffuse => &mut state.diffuse,
			ResourceKind::Normal => &mut state.normal,
			ResourceKind::Orm => &mut state.orm,
		};

		component.image = Some(image);
	}

	pub fn finalize(&self) {
		lock(&self.state).record.set_ready_to_check(true);
	}

	pub fn is_built(&self) -> bool {
		lock(&self.state).record.is_built()
	}

	pub fn is_transparent(&self) -> bool {
		lock(&self.state).record.is_transparent()
	}

	pub fn is_double_sided(&self) -> bool {
		lock(&self.state).record.has_flag(FLAG_DOUBLE_SIDED)
	}

	pub fn supports_skinning(&self) -> bool {
		lock(&self.state).record.supports_skinning
	}

	/// What the material needs from the pipelines that draw it
	pub fn pipeline_features(&self) -> u32 {
		let state = lock(&self.state);

		state
			.record
			.pipeline_features(state.normal.image.is_some() || state.orm.image.is_some())
	}

	/// Whether the material can be drawn
	pub fn is_ready(&self) -> bool {
		let mut state = lock(&self.state);

		let components = [
			state.diffuse.state(),
			state.normal.state(),
			state.orm.state(),
		];

		state.record.evaluate_ready(&components)
	}

	/// The descriptor set once the material is built, to bind as set 1
	pub fn descriptor_set_id(&self) -> Option<u32> {
		lock(&self.state).descriptor_set
	}

	/// Makes the descriptor set once every texture the material has is loaded. Returns whether the
	/// material is built.
	pub fn build(&self, gpu: &MaterialGpu) -> bool {
		let mut state = lock(&self.state);

		if state.diffuse.image.is_none() {
			state.diffuse.image = Some(gpu.assets.null_image(ImageFormat::Rgba8UNorm));
		}

		if state.diffuse.status() != ComponentStatus::Ready
			|| state.normal.status() == ComponentStatus::NotReady
			|| state.orm.status() == ComponentStatus::NotReady
		{
			return false;
		}

		let max_lod = state
			.diffuse
			.max_lod()
			.max(state.normal.max_lod())
			.max(state.orm.max_lod());

		let mut props = SamplerProps {
			min_lod: state.diffuse.min_lod(),
			max_lod,
			..SamplerProps::default()
		};

		if state.record.nearest_filtering {
			props.min_filter = Filter::Nearest;
			props.mag_filter = Filter::Nearest;
			props.mip_filter = Filter::Nearest;
			props.max_anisotropy = 1;
		} else {
			props.max_anisotropy = ANISOTROPY;
		}

		if state.normal.image.is_some() && state.orm.image.is_none() {
			state.orm.image = Some(gpu.assets.null_image(ImageFormat::Rgba8UNorm));
		}

		if state.descriptor_set.is_none() {
			let normal = state
				.normal
				.image
				.clone()
				.unwrap_or_else(|| gpu.assets.flat_normal_image());
			let orm = state
				.orm
				.image
				.clone()
				.unwrap_or_else(|| gpu.assets.null_image(ImageFormat::Rgba8UNorm));
			let diffuse = state
				.diffuse
				.image
				.clone()
				.expect("the diffuse image was just set");

			log_info!(Render; "** Building Material ({}) descriptor set", state.record.id);

			let sampler = gpu.gfx.sampler(&props);

			let buffers = gpu.gfx.buffers();
			let bone = lock_uniforms(&buffers.bone);
			let light = lock_uniforms(&buffers.light);

			let image_entry = |binding: u32, handle: &ImageHandle| DescriptorEntryRef {
				binding,
				stages: vk::ShaderStageFlags::FRAGMENT,
				resource: DescriptorResourceRef::Image {
					image: Arc::as_ptr(handle.image.record()),
					sampler,
				},
			};

			let entries = [
				image_entry(0, &diffuse),
				image_entry(1, &normal),
				image_entry(2, &orm),
				DescriptorEntryRef {
					binding: 3,
					stages: vk::ShaderStageFlags::VERTEX,
					resource: DescriptorResourceRef::Buffer {
						buffer: Arc::as_ptr(&bone.0),
						offset: 0,
						range: u64::from(bone.1),
					},
				},
				DescriptorEntryRef {
					binding: 4,
					stages: vk::ShaderStageFlags::FRAGMENT,
					resource: DescriptorResourceRef::Buffer {
						buffer: Arc::as_ptr(&light.0),
						offset: 0,
						range: u64::from(light.1),
					},
				},
			];

			// SAFETY: every record in the entries is a live `Arc` that the caller's handles keep
			// alive, and the layout cache is the device's own.
			let requested = unsafe {
				gpu.gfx
					.descriptors()
					.request(gpu.gfx.device(), gpu.gfx.ds_layouts(), &entries)
			};

			match requested {
				Ok((id, _)) => state.descriptor_set = Some(id),
				Err(error) => {
					log_error!(Render; "Could not make the descriptor set of material {}: {error:?}", self.name);
					return false;
				}
			}
		}

		state.record.set_built(true);

		true
	}
}

fn lock_uniforms(uniforms: &Mutex<raptor_gfx::Uniforms>) -> (Arc<BufferRecord>, u32) {
	let uniforms = lock(uniforms);

	(
		Arc::clone(uniforms.gpu_buffer().record()),
		uniforms.page_size(),
	)
}

/// What a material needs from the renderer to be built
pub struct MaterialGpu<'a> {
	pub gfx: &'a Gfx,
	pub assets: &'a AssetManager,
}

/// Every material, and the buffer of their properties that shaders read
pub struct MaterialStore {
	slots: Mutex<Vec<Option<Arc<Material>>>>,
	properties: GpuBuffer,
}

const NULL_COLORS: [[u8; 4]; 2] = [[255, 80, 203, 255], [0, 0, 0, 255]];

fn null_texture() -> TextureData {
	let mut bytes = Vec::with_capacity(4 * 4 * 4);

	for row in 0..4usize {
		for column in 0..4usize {
			let pink = ((row / 2) + (column / 2)).is_multiple_of(2);

			bytes.extend_from_slice(&NULL_COLORS[usize::from(!pink)]);
		}
	}

	TextureData {
		width: 4,
		height: 4,
		format: ImageFormat::Rgba8UNorm,
		mip_count: 1,
		bytes,
	}
}

impl MaterialStore {
	/// Makes the store with the null material at id 0, which is what an object without a material
	/// is drawn with
	pub fn new(gfx: &Gfx, assets: &AssetManager) -> Result<Self, vk::Result> {
		let properties = GpuBuffer::with_data(
			gfx.core(),
			BufferType::Storage,
			u64::from(MAX_MATERIALS)
				* size_of::<raptor_render::material::MaterialProperties>() as u64,
			Memory::AutoPreferDevice,
			raptor_gfx::buffer::FLAG_PERSISTENT_MAPPED,
		)?;

		let store = Self {
			slots: Mutex::new(Vec::new()),
			properties,
		};

		let mut record = MaterialRecord::default();
		record.reset(0);
		record.nearest_filtering = true;

		let null = Arc::new(Material::new("NullMaterial", record));

		null.attach(ResourceKind::Diffuse, assets.upload_image(null_texture()));
		null.attach(
			ResourceKind::Normal,
			assets.null_image(ImageFormat::Rgba8UNorm),
		);
		null.attach(
			ResourceKind::Orm,
			assets.null_image(ImageFormat::Rgba8UNorm),
		);
		null.finalize();

		lock(&store.slots).push(Some(null));

		log_info!(Core; "Created null material (Id=0)");

		Ok(store)
	}

	pub fn properties_buffer(&self) -> &GpuBuffer {
		&self.properties
	}

	pub fn get(&self, id: MaterialId) -> Option<Arc<Material>> {
		lock(&self.slots).get(id.0 as usize).cloned().flatten()
	}

	pub fn null_material(&self) -> Arc<Material> {
		self.get(MaterialId::NULL)
			.expect("the null material is made with the store")
	}

	/// A new material of the given name, or none if every slot is taken
	pub fn create(
		&self,
		name: &str,
		supports_skinning: bool,
	) -> Option<(MaterialId, Arc<Material>)> {
		let mut slots = lock(&self.slots);

		let index = slots
			.iter()
			.skip(1)
			.position(Option::is_none)
			.map(|index| index + 1)
			.or_else(|| (slots.len() < MAX_MATERIALS as usize).then_some(slots.len()))?;

		let mut record = MaterialRecord::default();
		record.reset(index as u32);
		record.supports_skinning = supports_skinning;

		let material = Arc::new(Material::new(name, record));

		if index == slots.len() {
			slots.push(Some(Arc::clone(&material)));
		} else {
			slots[index] = Some(Arc::clone(&material));
		}

		Some((MaterialId(index as u32), material))
	}

	/// Makes a material from what a model file describes, loading its textures
	pub fn create_from_desc(
		&self,
		desc: MaterialDesc,
		assets: &AssetManager,
	) -> Option<(MaterialId, Arc<Material>)> {
		let (id, material) = self.create(&desc.name, desc.record.supports_skinning)?;

		material.with_record(|record| {
			let id = record.id;
			record.copy_from(&desc.record);
			record.id = id;
			record.set_ready_to_check(desc.record.is_ready_to_check());
		});

		let textures = [
			(ResourceKind::Diffuse, desc.diffuse),
			(ResourceKind::Normal, desc.normal),
			(ResourceKind::Orm, desc.orm),
		];

		for (kind, texture) in textures {
			if let Some(texture) = texture {
				material.attach(kind, assets.upload_image(texture));
			}
		}

		Some((id, material))
	}

	/// Frees the material's slot and its descriptor set
	pub fn destroy(&self, gfx: &Gfx, id: MaterialId) {
		if id.is_null() {
			return;
		}

		let Some(material) = lock(&self.slots)
			.get_mut(id.0 as usize)
			.and_then(Option::take)
		else {
			return;
		};

		if let Some(set) = lock(&material.state).descriptor_set.take() {
			// SAFETY: the set belongs to this device and allocator, and the material is going
			// away so nothing the caller still draws with uses it.
			unsafe { gfx.descriptors().free(gfx.device(), gfx.allocator(), set) };
		}
	}

	/// Binds the material as set 1 of the pipeline layout. Builds the material first if it is not
	/// yet, and does nothing and returns false if it is not ready to be drawn. `offsets` are the
	/// dynamic offsets of the bone and light buffers, in that order.
	pub fn bind(
		&self,
		gpu: &MaterialGpu,
		id: MaterialId,
		cmd: vk::CommandBuffer,
		bind_point: vk::PipelineBindPoint,
		layout: vk::PipelineLayout,
		offsets: [u32; 2],
	) -> bool {
		let Some(material) = self.get(id) else {
			log_error!(Core; "Could not bind material {}", id.0);
			return false;
		};

		if let Err(error) = self.sync(&material) {
			log_error!(Render; "Could not sync material {}: {error:?}", id.0);
		}

		if !material.is_built() && !material.build(gpu) {
			return false;
		}

		if !material.is_ready() {
			return false;
		}

		let Some(set) = material.descriptor_set_id() else {
			return false;
		};

		let mut descriptors = gpu.gfx.descriptors();

		let Some(record) = descriptors.find(set) else {
			return false;
		};

		// SAFETY: the record is boxed in the cache, which is locked, and the caller is recording
		// into `cmd` with a pipeline of `layout`.
		unsafe { (*record).bind(gpu.gfx.device(), cmd, bind_point, layout, 1, &offsets) };

		true
	}

	/// Writes the material's properties where shaders read them, if they changed
	pub fn sync(&self, material: &Material) -> Result<(), vk::Result> {
		let mut state = lock(&material.state);

		if !state.record.requires_sync() {
			return Ok(());
		}

		let index = state.record.id as usize;
		let size = size_of::<raptor_render::material::MaterialProperties>();
		let properties = state.record.properties;

		// SAFETY: the properties are plain data, laid out as the shader reads them.
		let bytes = unsafe {
			std::slice::from_raw_parts(std::ptr::from_ref(&properties).cast::<u8>(), size)
		};

		self.properties.with_mapped(|mapped| {
			mapped[index * size..(index + 1) * size].copy_from_slice(bytes);
		})?;

		state.record.mark_synced();

		Ok(())
	}

	pub fn count(&self) -> usize {
		lock(&self.slots).iter().flatten().count()
	}
}
