use std::ffi::{CString, c_void};

use raptor_gpu::{BlendAttachment, DescriptorEntryRef, RenderStageRecord};

pub const NO_SHADER: u32 = u32::MAX;

pub type DeclareFn = unsafe extern "C" fn(user: *mut c_void, desc: *mut PipelineDesc);
pub type FreeFn = unsafe extern "C" fn(user: *mut c_void);

pub struct Declarer
{
	call: DeclareFn,
	user: *mut c_void,
	free: Option<FreeFn>,
}

impl Declarer
{
	/// # Safety
	///
	/// `call` must accept `user` and a description, and may be called on the thread that builds
	/// pipelines. `free` is called with `user` once, when the declarer is dropped.
	pub unsafe fn new(call: DeclareFn, user: *mut c_void, free: Option<FreeFn>) -> Self
	{
		Self { call, user, free }
	}

	pub fn declare(&self, desc: &mut PipelineDesc)
	{
		// SAFETY: guaranteed by the creator of the declarer.
		unsafe { (self.call)(self.user, desc) };
	}
}

impl Drop for Declarer
{
	fn drop(&mut self)
	{
		if let Some(free) = self.free {
			// SAFETY: guaranteed by the creator of the declarer.
			unsafe { free(self.user) };
		}
	}
}

// SAFETY: the host promised the callbacks can be called, and the user data dropped, on the thread
// that builds pipelines, which is where a declarer is used and dropped.
unsafe impl Send for Declarer {}

pub struct MacroDef
{
	pub name: Option<CString>,
	pub value: Option<CString>,
}

pub struct PipelineDesc
{
	pub debug_name: String,

	pub shader_index: u32,
	pub shader_name: String,
	pub macros: Vec<MacroDef>,

	pub vertex_type: u32,
	pub no_vertices: bool,

	pub stage: *mut RenderStageRecord,

	pub cull_mode: u32,
	pub front_face: i32,
	pub polygon_mode: i32,
	pub depth_compare_op: i32,
	pub depth_test: bool,
	pub depth_write: bool,
	pub render_lines: bool,

	pub blends: Vec<BlendAttachment>,

	pub push_constants: Vec<(u32, u32)>,

	pub sets: Vec<Vec<DescriptorEntryRef>>,

	pub features: u32,

	pub declare: Option<Declarer>,
}

// SAFETY: the stage and the resources of the descriptor entries are only used when the pipeline
// is built, on the thread that builds them, and the host keeps them alive until then.
unsafe impl Send for PipelineDesc {}

const CULL_BACK: u32 = 2;
const FRONT_FACE_COUNTER_CLOCKWISE: i32 = 0;
const POLYGON_MODE_FILL: i32 = 0;
const COMPARE_OP_GREATER_OR_EQUAL: i32 = 6;
const VERTEX_TYPE_DEFAULT: u32 = 1;

impl Default for PipelineDesc
{
	fn default() -> Self
	{
		Self {
			debug_name: "Pipeline".to_owned(),
			shader_index: NO_SHADER,
			shader_name: String::new(),
			macros: Vec::new(),
			vertex_type: VERTEX_TYPE_DEFAULT,
			no_vertices: false,
			stage: std::ptr::null_mut(),
			cull_mode: CULL_BACK,
			front_face: FRONT_FACE_COUNTER_CLOCKWISE,
			polygon_mode: POLYGON_MODE_FILL,
			depth_compare_op: COMPARE_OP_GREATER_OR_EQUAL,
			depth_test: true,
			depth_write: true,
			render_lines: false,
			blends: Vec::new(),
			push_constants: Vec::new(),
			sets: Vec::new(),
			features: 0,
			declare: None,
		}
	}
}

impl PipelineDesc
{
	pub fn add_entry(&mut self, set: usize, entry: DescriptorEntryRef)
	{
		if self.sets.len() <= set {
			self.sets.resize_with(set + 1, Vec::new);
		}

		self.sets[set].push(entry);
	}

	pub fn set_shader(&mut self, index: u32, name: &str)
	{
		self.shader_index = index;
		self.shader_name = name.to_owned();
	}

	pub fn add_macro(&mut self, name: Option<&[u8]>, value: Option<&[u8]>)
	{
		let to_c = |bytes: &[u8]| CString::new(bytes).ok();

		self.macros.push(MacroDef {
			name: name.and_then(to_c),
			value: value.and_then(to_c),
		});
	}

	pub fn add_blend(&mut self, target_index: u32, mut blend: BlendAttachment)
	{
		blend.target_index = target_index;
		self.blends.push(blend);
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn entries_are_kept_by_the_set_they_were_added_to()
	{
		let mut desc = PipelineDesc::default();

		let entry = |binding| DescriptorEntryRef {
			binding,
			stages: raptor_gpu::vk::ShaderStageFlags::VERTEX,
			resource: raptor_gpu::DescriptorResourceRef::Image {
				image: std::ptr::null(),
				sampler: raptor_gpu::vk::Sampler::null(),
			},
		};

		desc.add_entry(2, entry(5));
		desc.add_entry(0, entry(1));
		desc.add_entry(2, entry(6));

		assert_eq!(desc.sets.len(), 3);
		assert_eq!(desc.sets[0].len(), 1);
		assert!(desc.sets[1].is_empty());
		assert_eq!(desc.sets[2].len(), 2);
		assert_eq!(desc.sets[2][1].binding, 6);
	}

	#[test]
	fn a_blend_takes_the_target_it_was_added_for()
	{
		let mut desc = PipelineDesc::default();

		desc.add_blend(3, BlendAttachment::default());

		assert_eq!(desc.blends[0].target_index, 3);
	}

	#[test]
	fn the_defaults_are_the_ones_pipelines_were_made_with()
	{
		let desc = PipelineDesc::default();

		assert_eq!(desc.cull_mode, raptor_gpu::vk::CullModeFlags::BACK.as_raw());
		assert_eq!(
			desc.depth_compare_op,
			raptor_gpu::vk::CompareOp::GREATER_OR_EQUAL.as_raw()
		);
		assert_eq!(desc.vertex_type, raptor_gpu::VertexType::Default as u32);
		assert_eq!(
			desc.polygon_mode,
			raptor_gpu::vk::PolygonMode::FILL.as_raw()
		);
		assert_eq!(
			desc.front_face,
			raptor_gpu::vk::FrontFace::COUNTER_CLOCKWISE.as_raw()
		);
	}
}
