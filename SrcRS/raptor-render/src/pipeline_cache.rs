use std::collections::HashMap;
use std::ffi::{CString, c_char, c_void};
use std::sync::atomic::{AtomicBool, AtomicPtr, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::{self, ThreadId};

use raptor_gpu::vk::{self, Handle};
use raptor_gpu::{
	DescriptorCache, DescriptorSetRecord, Device, DsLayoutCache, INVALID_INDEX, KeyRegistration,
	Level, NUM_PASSES, PipelineLayoutRecord, PipelineRecord, PipelineRegistry, ShaderProgramRecord,
	VertexType, blend_states, filter_by_input_mask,
};
use raptor_shader::preproc::Stage;
use raptor_shader::program::{MacroRef, hash_macros};
use raptor_shader::{Log, LogLevel};

use crate::pipeline_builder::{BuildError, PipelineBuild, PipelineBuilt, build_pipeline};
use crate::pipeline_desc::PipelineDesc;
use crate::shader_library::{LoadError, ShaderLibrary};

const HASH_INIT: u64 = 0xCBF2_9CE4_8422_2325;

pub const MAX_OFFSET_SETS: usize = 8;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SlotSet
{
	pub index: u32,
	pub set: *mut DescriptorSetRecord,
	pub layout: u64,
}

#[repr(C)]
pub struct PipelineSlot
{
	pub pipeline: u64,
	pub layout: u64,
	pub default_cull_mode: u32,
	pub handle: u32,
	pub name: u32,
	pub set_count: u32,
	pub sets: *const SlotSet,
	pub debug_name: *const c_char,
	pub is_compute: u8,
	pub built: u8,

	record: Option<Box<PipelineRecord>>,
	layout_record: Option<Arc<PipelineLayoutRecord>>,
	set_list: Vec<SlotSet>,
	programs: Vec<Arc<ShaderProgramRecord>>,
	name_storage: CString,
}

impl PipelineSlot
{
	fn new(handle: u32, name: u32, debug_name: &str) -> Box<Self>
	{
		let mut slot = Box::new(Self {
			pipeline: 0,
			layout: 0,
			default_cull_mode: 0,
			handle,
			name,
			set_count: 0,
			sets: std::ptr::null(),
			debug_name: std::ptr::null(),
			is_compute: 0,
			built: 0,
			record: None,
			layout_record: None,
			set_list: Vec::new(),
			programs: Vec::new(),
			name_storage: CString::new(debug_name).unwrap_or_default(),
		});

		slot.debug_name = slot.name_storage.as_ptr();

		slot
	}

	pub fn is_built(&self) -> bool
	{
		self.built != 0
	}

	fn release(&mut self, device: &Device)
	{
		if let Some(record) = self.record.take() {
			// SAFETY: the pipeline was made on this device, and the caller has waited for it to be
			// idle.
			unsafe { record.destroy(device) };
		}

		if let Some(layout) = self.layout_record.take() {
			// SAFETY: as above.
			unsafe { PipelineLayoutRecord::release(Arc::into_raw(layout), Some(device)) };
		}

		for program in self.programs.drain(..) {
			// SAFETY: as above.
			unsafe { ShaderProgramRecord::release_arc(program, device) };
		}

		self.set_list.clear();
		self.built = 0;
		self.pipeline = 0;
		self.layout = 0;
		self.set_count = 0;
		self.sets = std::ptr::null();
	}

	fn leak(&mut self)
	{
		std::mem::forget(self.record.take());
		std::mem::forget(self.layout_record.take());
		std::mem::forget(std::mem::take(&mut self.programs));
	}
}

pub type TemplateFn = unsafe extern "C" fn(
	user: *mut c_void,
	pass: u32,
	features: u32,
	desc: *mut PipelineDesc,
) -> bool;

pub type TemplateFree = unsafe extern "C" fn(user: *mut c_void);

struct Template
{
	call: TemplateFn,
	user: *mut c_void,
	free: Option<TemplateFree>,
}

impl Drop for Template
{
	fn drop(&mut self)
	{
		if let Some(free) = self.free {
			// SAFETY: guaranteed by the creator of the template.
			unsafe { free(self.user) };
		}
	}
}

// SAFETY: the host promised the callbacks can be called from any thread, and the user data dropped
// on the thread that destroys the cache.
unsafe impl Send for Template {}

#[derive(Debug, PartialEq, Eq)]
pub enum BuildFailure
{
	DescriptorMismatch,
	Vulkan(i32),
	ShaderCompile,
	ShaderCompilerUnavailable,
	BlendTarget,
}

pub trait Builder
{
	fn build(
		&mut self,
		cache: &PipelineCache,
		handle: u32,
		desc: PipelineDesc,
	) -> Result<(), BuildFailure>;
}

struct Inner
{
	templates: [Option<Template>; NUM_PASSES],
	descs: HashMap<u32, PipelineDesc>,
}

pub struct PipelineCache
{
	registry: PipelineRegistry,
	num_static: u32,
	statics: Vec<AtomicPtr<PipelineSlot>>,
	dynamics: Vec<AtomicPtr<PipelineSlot>>,
	inner: Mutex<Inner>,
	offsets: Mutex<[Vec<u32>; MAX_OFFSET_SETS]>,
	main_thread: ThreadId,
	building: AtomicBool,
}

// SAFETY: slots are only changed on the thread that builds pipelines, other threads only look up
// handles through the registry, and the pointers in the cache are owned by it.
unsafe impl Sync for PipelineCache {}
// SAFETY: as above.
unsafe impl Send for PipelineCache {}

impl PipelineCache
{
	pub fn new(num_static: u32, max_dynamic: u32) -> Self
	{
		let statics = (0..num_static)
			.map(|index| AtomicPtr::new(Box::into_raw(PipelineSlot::new(index, index, ""))))
			.collect();

		let dynamics = (0..max_dynamic)
			.map(|_| AtomicPtr::new(std::ptr::null_mut()))
			.collect();

		Self {
			registry: PipelineRegistry::new(num_static, max_dynamic),
			num_static,
			statics,
			dynamics,
			inner: Mutex::new(Inner {
				templates: std::array::from_fn(|_| None),
				descs: HashMap::new(),
			}),
			offsets: Mutex::new(std::array::from_fn(|_| Vec::new())),
			main_thread: thread::current().id(),
			building: AtomicBool::new(false),
		}
	}

	fn lock(&self) -> MutexGuard<'_, Inner>
	{
		self.inner
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())
	}

	pub fn registry(&self) -> &PipelineRegistry
	{
		&self.registry
	}

	pub fn num_static(&self) -> u32
	{
		self.num_static
	}

	pub fn is_main_thread(&self) -> bool
	{
		thread::current().id() == self.main_thread
	}

	pub fn has_pending(&self) -> bool
	{
		self.registry.has_pending()
	}

	pub fn slot(&self, handle: u32) -> *mut PipelineSlot
	{
		if handle < self.num_static {
			return self.statics[handle as usize].load(Ordering::Acquire);
		}

		self.dynamics
			.get((handle - self.num_static) as usize)
			.map_or(std::ptr::null_mut(), |slot| slot.load(Ordering::Acquire))
	}

	pub fn set_static_name(&self, handle: u32, name: &str)
	{
		let slot = self.slot(handle);

		if slot.is_null() {
			return;
		}

		// SAFETY: slots are only changed on the thread that builds pipelines, which is this one.
		let slot = unsafe { &mut *slot };

		slot.name_storage = CString::new(name).unwrap_or_default();
		slot.debug_name = slot.name_storage.as_ptr();
	}

	pub fn register_template(
		&self,
		pass: u32,
		call: TemplateFn,
		user: *mut c_void,
		free: Option<TemplateFree>,
	)
	{
		assert!((pass as usize) < NUM_PASSES);

		self.lock().templates[pass as usize] = Some(Template { call, user, free });
	}

	fn create_locked(&self, inner: &mut Inner, desc: PipelineDesc) -> Option<u32>
	{
		let handle = self.registry.allocate()?;

		let slot = Box::into_raw(PipelineSlot::new(handle, INVALID_INDEX, &desc.debug_name));

		self.dynamics[(handle - self.num_static) as usize].store(slot, Ordering::Release);

		inner.descs.insert(handle, desc);

		Some(handle)
	}

	pub fn create(
		&self,
		desc: PipelineDesc,
		builder: &mut dyn Builder,
	) -> Result<Option<u32>, BuildFailure>
	{
		let handle = self.create_locked(&mut self.lock(), desc);

		let Some(handle) = handle else {
			return Ok(None);
		};

		if self.is_main_thread() {
			self.build_pending(builder)?;

			// SAFETY: the slot was made by `create_locked`.
			if !unsafe { &*self.slot(handle) }.is_built() {
				return Ok(None);
			}
		}

		Ok(Some(handle))
	}

	pub fn build_pending(&self, builder: &mut dyn Builder) -> Result<(), BuildFailure>
	{
		assert!(self.is_main_thread());

		if self.building.swap(true, Ordering::AcqRel) {
			return Ok(());
		}

		let mut failure = None;

		loop {
			let pending = {
				let mut inner = self.lock();

				self.registry
					.take_pending()
					.into_iter()
					.filter_map(|index| {
						let handle = self.num_static + index;
						inner.descs.remove(&handle).map(|desc| (handle, desc))
					})
					.collect::<Vec<_>>()
			};

			if pending.is_empty() {
				break;
			}

			for (handle, desc) in pending {
				if let Err(error) = builder.build(self, handle, desc) {
					failure.get_or_insert(error);
				}
			}
		}

		self.building.store(false, Ordering::Release);

		failure.map_or(Ok(()), Err)
	}

	pub fn build_static(
		&self,
		handle: u32,
		desc: PipelineDesc,
		builder: &mut dyn Builder,
	) -> Result<(), BuildFailure>
	{
		assert!(handle < self.num_static);

		builder.build(self, handle, desc)
	}

	pub fn find_variant(&self, pass: u32, features: u32) -> u32
	{
		self.registry.find_variant(pass, features)
	}

	pub fn find_variant_in_pass(&self, handle: u32, pass: u32) -> u32
	{
		match self.registry.variant_info(handle) {
			Some((_, features)) => self.registry.find_variant(pass, features),
			None => INVALID_INDEX,
		}
	}

	pub fn get_or_create_variant(
		&self,
		pass: u32,
		features: u32,
		builder: &mut dyn Builder,
	) -> Result<u32, BuildFailure>
	{
		let found = self.registry.find_variant(pass, features);

		if found != INVALID_INDEX {
			return Ok(found);
		}

		let handle = {
			let mut inner = self.lock();

			let found = self.registry.find_variant(pass, features);

			if found != INVALID_INDEX {
				return Ok(found);
			}

			let Some((call, user)) = inner
				.templates
				.get(pass as usize)
				.and_then(Option::as_ref)
				.map(|template| (template.call, template.user))
			else {
				return Ok(INVALID_INDEX);
			};

			let mut desc = PipelineDesc::default();

			// SAFETY: guaranteed by the creator of the template.
			if !unsafe { call(user, pass, features, &mut desc) } {
				return Ok(INVALID_INDEX);
			}

			let canonical = desc.features;

			let mut handle = self.registry.find_variant(pass, canonical);

			if handle == INVALID_INDEX {
				let Some(created) = self.create_locked(&mut inner, desc) else {
					return Ok(INVALID_INDEX);
				};

				handle = created;
				self.registry.register_variant(pass, canonical, handle);
			}

			if features != canonical {
				self.registry.register_variant(pass, features, handle);
			}

			handle
		};

		if self.is_main_thread() {
			self.build_pending(builder)?;
		}

		Ok(handle)
	}

	pub fn get_or_create_variant_in_pass(
		&self,
		handle: u32,
		pass: u32,
		builder: &mut dyn Builder,
	) -> Result<u32, BuildFailure>
	{
		match self.registry.variant_info(handle) {
			Some((_, features)) => self.get_or_create_variant(pass, features, builder),
			None => Ok(INVALID_INDEX),
		}
	}

	pub fn add_buffer_offset(&self, set: usize, offset: u32)
	{
		self.offsets
			.lock()
			.unwrap_or_else(|poisoned| poisoned.into_inner())[set]
			.push(offset);
	}

	pub fn take_offsets(&self) -> [Vec<u32>; MAX_OFFSET_SETS]
	{
		std::mem::take(
			&mut *self
				.offsets
				.lock()
				.unwrap_or_else(|poisoned| poisoned.into_inner()),
		)
	}

	pub fn install(
		&self,
		device: &Device,
		handle: u32,
		built: PipelineBuilt,
		programs: Vec<Arc<ShaderProgramRecord>>,
	) -> KeyRegistration
	{
		let slot = self.slot(handle);

		assert!(!slot.is_null());

		// SAFETY: slots are only changed on the thread that builds pipelines, which is this one.
		let slot = unsafe { &mut *slot };

		if slot.is_built() {
			device.wait_idle();
			slot.release(device);
		}

		slot.pipeline = built.pipeline.fields.pipeline;
		slot.default_cull_mode = built.pipeline.fields.default_cull_mode;
		slot.is_compute = built.pipeline.fields.is_compute;
		slot.layout = built.layout.fields.layout;

		slot.set_list = built
			.sets
			.iter()
			.map(|set| SlotSet {
				index: set.index,
				set: set.set,
				layout: set.layout.as_raw(),
			})
			.collect();
		slot.set_count = slot.set_list.len() as u32;
		slot.sets = slot.set_list.as_ptr();

		slot.record = Some(built.pipeline);
		slot.layout_record = Some(built.layout);
		slot.programs = programs;
		slot.built = 1;

		self.registry.register_key(handle, &built.key)
	}

	pub fn destroy(self, device: Option<&Device>)
	{
		if let Some(device) = device {
			device.wait_idle();
		}

		for slot in self.statics.iter().chain(&self.dynamics) {
			let pointer = slot.swap(std::ptr::null_mut(), Ordering::AcqRel);

			if pointer.is_null() {
				continue;
			}

			// SAFETY: the pointer came from `Box::into_raw` and nothing else has it.
			let mut slot = unsafe { Box::from_raw(pointer) };

			match device {
				Some(device) => slot.release(device),
				None => slot.leak(),
			}
		}
	}
}

pub struct GpuBuilder<'a>
{
	pub device: &'a Device,
	pub descriptors: &'a mut DescriptorCache,
	pub ds_layouts: &'a DsLayoutCache,
	pub library: &'a mut ShaderLibrary,
	pub log: &'a mut dyn Log,
}

fn stage_programs(
	builder: &mut GpuBuilder,
	desc: &PipelineDesc,
	macros: &[MacroRef],
) -> Result<[Option<Arc<ShaderProgramRecord>>; 3], BuildFailure>
{
	let mut programs = [None, None, None];

	for (slot, stage) in programs
		.iter_mut()
		.zip([Stage::Vertex, Stage::Pixel, Stage::Compute])
	{
		*slot = builder
			.library
			.program(
				builder.device,
				&desc.shader_name,
				stage,
				macros,
				&mut *builder.log,
			)
			.map_err(|error| match error {
				LoadError::CompileFailed => BuildFailure::ShaderCompile,
				LoadError::CompilerUnavailable => BuildFailure::ShaderCompilerUnavailable,
			})?;
	}

	Ok(programs)
}

impl Builder for GpuBuilder<'_>
{
	fn build(
		&mut self,
		cache: &PipelineCache,
		handle: u32,
		mut desc: PipelineDesc,
	) -> Result<(), BuildFailure>
	{
		if let Some(declarer) = desc.declare.take() {
			declarer.declare(&mut desc);
		}

		let macros: Vec<MacroRef> = desc
			.macros
			.iter()
			.map(|definition| MacroRef {
				name: definition.name.as_deref().map(std::ffi::CStr::to_bytes),
				value: definition.value.as_deref().map(std::ffi::CStr::to_bytes),
			})
			.collect();

		let macro_hash = hash_macros(&macros, HASH_INIT);

		let programs = stage_programs(self, &desc, &macros)?;

		let [vertex, pixel, compute] = &programs;

		let is_compute = compute.is_some() && vertex.is_none() && pixel.is_none();

		// SAFETY: the host keeps the stage alive until the pipeline is built.
		let stage = unsafe { desc.stage.as_mut() };

		let has_vertex_input = !desc.no_vertices;

		let vertex_type = VertexType::from_raw(desc.vertex_type);

		if vertex_type.is_none() {
			self.device
				.log()
				.log(Level::Error, "Unsupported vertex type!");
		}

		let mut attributes = vertex_type.map(VertexType::attributes).unwrap_or_default();

		if has_vertex_input && let Some(vertex) = vertex {
			attributes = filter_by_input_mask(&attributes, vertex.fields.input_location_mask);
		}

		let color_blend = match stage.as_deref() {
			Some(stage) if !is_compute => {
				let target_count = stage.targets().len() as u32;
				let color_count = stage.targets().color_formats().len() as u32;

				let blends: Vec<_> = desc
					.blends
					.iter()
					.filter(|blend| blend.target_index <= target_count)
					.copied()
					.collect();

				blend_states(&blends, color_count).map_err(|_| BuildFailure::BlendTarget)?
			}
			_ => Vec::new(),
		};

		let use_vertex_input = has_vertex_input && !is_compute;

		let build = PipelineBuild {
			debug_name: &desc.debug_name,
			shader: desc.shader_index,
			macro_hash,
			vertex: vertex.as_deref(),
			pixel: pixel.as_deref(),
			compute: compute.as_deref(),
			vertex_type: desc.vertex_type,
			has_vertex_input,
			vertex_binding: vertex_type
				.filter(|_| use_vertex_input)
				.map(VertexType::binding),
			vertex_attributes: if use_vertex_input { &attributes } else { &[] },
			color_blend: &color_blend,
			cull_mode: vk::CullModeFlags::from_raw(desc.cull_mode),
			front_face: vk::FrontFace::from_raw(desc.front_face),
			polygon_mode: vk::PolygonMode::from_raw(desc.polygon_mode),
			depth_compare_op: vk::CompareOp::from_raw(desc.depth_compare_op),
			render_lines: desc.render_lines,
			disable_depth_test: !desc.depth_test,
			disable_depth_write: !desc.depth_write,
			push_constants: &desc.push_constants,
			sets: &desc.sets,
		};

		// SAFETY: the host keeps everything the description refers to alive until now, and all of
		// it belongs to this device.
		let result = unsafe {
			build_pipeline(
				self.device,
				self.descriptors,
				self.ds_layouts,
				stage,
				&build,
			)
		};

		match result {
			Ok(built) => {
				let programs: Vec<_> = programs.into_iter().flatten().collect();

				match cache.install(self.device, handle, built, programs) {
					KeyRegistration::Registered => {}
					KeyRegistration::IdenticalKey { other, hash } => {
						self.log.log(
							LogLevel::Warning,
							&format!(
								"Pipelines {handle} and {other} were built from identical keys (hash {hash:#x})"
							),
						);
					}
					KeyRegistration::HashCollision { other, hash } => {
						self.log.log(
							LogLevel::Error,
							&format!(
								"Pipelines {handle} and {other} have different keys with the same hash {hash:#x}"
							),
						);
					}
				}

				Ok(())
			}
			Err(BuildError::InvalidShaders | BuildError::NoRenderPass) => Ok(()),
			Err(BuildError::DescriptorMismatch) => Err(BuildFailure::DescriptorMismatch),
			Err(BuildError::Vulkan(result)) => Err(BuildFailure::Vulkan(result.as_raw())),
		}
	}
}

#[cfg(test)]
mod tests
{
	use std::sync::atomic::AtomicU32;

	use super::*;

	struct Fake
	{
		built: Vec<(u32, String)>,
	}

	impl Builder for Fake
	{
		fn build(
			&mut self,
			_cache: &PipelineCache,
			handle: u32,
			desc: PipelineDesc,
		) -> Result<(), BuildFailure>
		{
			self.built.push((handle, desc.debug_name));
			Ok(())
		}
	}

	static CALLS: AtomicU32 = AtomicU32::new(0);

	unsafe extern "C" fn template(
		_user: *mut c_void,
		_pass: u32,
		features: u32,
		desc: *mut PipelineDesc,
	) -> bool
	{
		CALLS.fetch_add(1, Ordering::SeqCst);

		if features & 4 != 0 {
			return false;
		}

		// SAFETY: the cache passes a live description.
		let desc = unsafe { &mut *desc };

		desc.debug_name = format!("Variant{}", features & 1);
		desc.features = features & 1;

		true
	}

	fn cache_with_template() -> PipelineCache
	{
		let cache = PipelineCache::new(3, 8);

		cache.register_template(1, template, std::ptr::null_mut(), None);

		cache
	}

	#[test]
	fn a_variant_is_made_once_and_found_afterwards()
	{
		let cache = cache_with_template();
		let mut fake = Fake { built: Vec::new() };

		let first = cache.get_or_create_variant(1, 1, &mut fake).unwrap();
		let second = cache.get_or_create_variant(1, 1, &mut fake).unwrap();

		assert_eq!(first, 3);
		assert_eq!(first, second);
		assert_eq!(fake.built, vec![(3, "Variant1".to_owned())]);
		assert_eq!(cache.find_variant(1, 1), 3);
	}

	#[test]
	fn features_that_canonicalise_to_the_same_pipeline_share_it()
	{
		let cache = cache_with_template();
		let mut fake = Fake { built: Vec::new() };

		let plain = cache.get_or_create_variant(1, 0, &mut fake).unwrap();
		let extra = cache.get_or_create_variant(1, 2, &mut fake).unwrap();

		assert_eq!(plain, extra);
		assert_eq!(fake.built.len(), 1);
		assert_eq!(cache.find_variant(1, 2), plain);
	}

	#[test]
	fn a_template_that_declines_makes_nothing()
	{
		let cache = cache_with_template();
		let mut fake = Fake { built: Vec::new() };

		assert_eq!(
			cache.get_or_create_variant(1, 4, &mut fake).unwrap(),
			INVALID_INDEX
		);
		assert!(fake.built.is_empty());
		assert_eq!(
			cache.get_or_create_variant(2, 0, &mut fake).unwrap(),
			INVALID_INDEX
		);
	}

	#[test]
	fn a_variant_can_be_found_in_another_pass()
	{
		let cache = cache_with_template();
		cache.register_template(2, template, std::ptr::null_mut(), None);

		let mut fake = Fake { built: Vec::new() };

		let forward = cache.get_or_create_variant(1, 1, &mut fake).unwrap();

		assert_eq!(cache.find_variant_in_pass(forward, 2), INVALID_INDEX);

		let other = cache
			.get_or_create_variant_in_pass(forward, 2, &mut fake)
			.unwrap();

		assert_ne!(other, forward);
		assert_eq!(cache.find_variant_in_pass(forward, 2), other);
	}

	#[test]
	fn pipelines_asked_for_on_other_threads_are_built_on_the_main_thread()
	{
		let cache = std::sync::Arc::new(cache_with_template());

		let handle = {
			let cache = std::sync::Arc::clone(&cache);

			thread::spawn(move || {
				let mut fake = Fake { built: Vec::new() };
				let handle = cache.get_or_create_variant(1, 1, &mut fake).unwrap();

				assert!(fake.built.is_empty());

				handle
			})
			.join()
			.unwrap()
		};

		assert!(cache.has_pending());

		let mut fake = Fake { built: Vec::new() };

		cache.build_pending(&mut fake).unwrap();

		assert_eq!(fake.built.len(), 1);
		assert_eq!(fake.built[0].0, handle);
		assert!(!cache.has_pending());
	}

	#[test]
	fn a_pipeline_the_builder_did_not_install_is_not_made()
	{
		let cache = PipelineCache::new(1, 2);
		let mut fake = Fake { built: Vec::new() };

		assert_eq!(
			cache.create(PipelineDesc::default(), &mut fake).unwrap(),
			None
		);
		assert_eq!(fake.built.len(), 1);
	}

	#[test]
	fn the_number_of_pipelines_made_from_descriptions_is_limited()
	{
		let cache = PipelineCache::new(1, 1);
		let mut fake = Fake { built: Vec::new() };

		cache.create(PipelineDesc::default(), &mut fake).unwrap();
		cache.create(PipelineDesc::default(), &mut fake).unwrap();

		assert_eq!(fake.built.len(), 1);
	}
}
