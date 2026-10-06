use std::sync::Arc;

use ash::vk;
use raptor_core::log_warn;
use raptor_gfx::{Gfx, GpuBuffer, GpuCore, Image};
use raptor_gpu::{AddressMode, BorderColor, BufferType, Filter, Memory, SamplerProps, VertexType};
use raptor_math::mat4;

use crate::descriptors::{DescriptorSet, Entry};
use crate::names::PipelineName;
use crate::pipelines::Pipelines;
use crate::primitive_mesh::PrimitiveMesh;
use crate::push_constants::TextPushConstants;
use crate::text::{
	INSTANCE_SIZE, InstanceData, MARGIN, MAX_GLYPHS, TextState, image_instance, layout_text,
};

const VERTEX: vk::ShaderStageFlags = vk::ShaderStageFlags::VERTEX;
const PIXEL: vk::ShaderStageFlags = vk::ShaderStageFlags::FRAGMENT;

fn nearest() -> SamplerProps {
	SamplerProps {
		min_filter: Filter::Nearest,
		mag_filter: Filter::Nearest,
		mip_filter: Filter::Nearest,
		..SamplerProps::default()
	}
}

pub struct TextRenderer {
	state: TextState,
	instance_buffer: GpuBuffer,
	quad: PrimitiveMesh,
	atlas: Option<Image>,
	atlas_set: Option<DescriptorSet>,
}

impl TextRenderer {
	pub fn new(gfx: &Gfx) -> Self {
		let core: &Arc<GpuCore> = gfx.core();

		let instance_buffer = GpuBuffer::with_data(
			core,
			BufferType::StorageWithOffset,
			u64::from(INSTANCE_SIZE)
				* MAX_GLYPHS as u64
				* u64::from(raptor_gfx::limits::FRAMES_IN_FLIGHT),
			Memory::CpuOnly,
			raptor_gfx::buffer::FLAG_PERSISTENT_MAPPED,
		)
		.expect("the text instance buffer can be made");

		let mut quad =
			PrimitiveMesh::from_mesh(core, &raptor_mesh::quad(1.0, 1.0), VertexType::Default);

		quad.upload_now(gfx).expect("the text quad can be uploaded");

		let renderer = Self {
			state: TextState::default(),
			instance_buffer,
			quad,
			atlas: None,
			atlas_set: None,
		};

		renderer
	}

	pub fn instance_buffer(&self) -> &GpuBuffer {
		&self.instance_buffer
	}

	pub fn atlas(&self) -> Option<&Image> {
		self.atlas.as_ref()
	}

	pub fn set_atlas(&mut self, gfx: &Gfx, atlas: Image) {
		let entries = [
			Entry::Buffer {
				binding: 0,
				stages: VERTEX,
				buffer: self.instance_buffer.record(),
				offset: 0,
				range: u64::from(INSTANCE_SIZE) * MAX_GLYPHS as u64,
			},
			Entry::Image {
				binding: 1,
				stages: PIXEL,
				image: atlas.record(),
				sampler: gfx.sampler(&nearest()),
			},
		];

		self.atlas_set = Some(DescriptorSet::request(gfx, &entries));
		self.atlas = Some(atlas);
	}

	fn submit_quads(
		&mut self,
		gfx: &Gfx,
		pipelines: &Pipelines,
		set: DescriptorSet,
		instances: &[InstanceData],
		color: u32,
		is_image: bool,
	) -> bool {
		let Some(tape_offset) = self.state.reserve(instances.len() as u32) else {
			log_warn!(Render; "TextRenderer: Too many quads in frame, dropping draw");
			return false;
		};

		let cmd = gfx.frame_cmd();

		let name = if is_image {
			PipelineName::ImageRendering
		} else {
			PipelineName::TextRendering
		};

		let base_offset = gfx.frame_number() * MAX_GLYPHS as u32 * INSTANCE_SIZE;

		let bytes = bytemuck::cast_slice::<InstanceData, u8>(instances);

		// SAFETY: the buffer is persistently mapped and the tape holds `MAX_GLYPHS` quads for each
		// frame in flight, which `reserve` keeps the copy inside of.
		unsafe {
			std::ptr::copy_nonoverlapping(
				bytes.as_ptr(),
				self.instance_buffer
					.mapped_ptr()
					.add((base_offset + tape_offset) as usize),
				bytes.len(),
			)
		};

		pipelines.add_buffer_offset(0, base_offset);
		pipelines.bind_named(gfx, cmd, name);

		let Some(slot) = pipelines.slot(gfx, name.handle()) else {
			return false;
		};

		set.bind(gfx, cmd, slot, 0, &[base_offset]);

		let push = TextPushConstants {
			combined_matrix: {
				let (width, height) = gfx.window().size();

				mat4::flatten(&mat4::orthographic(width as f32, height as f32, 0.1, 10.0))
			},
			text_color: color,
			instance_base: tape_offset / INSTANCE_SIZE,
			..TextPushConstants::default()
		};

		pipelines.push_constants(gfx, cmd, slot, VERTEX, bytemuck::bytes_of(&push));

		self.quad.render(gfx, cmd, instances.len() as u32);

		true
	}

	fn draw_text_from(
		&mut self,
		gfx: &Gfx,
		pipelines: &Pipelines,
		text: &str,
		origin: [f32; 2],
		scale: f32,
		color: u32,
		advance_cursor: bool,
	) {
		let (Some(atlas), Some(set)) = (&self.atlas, self.atlas_set) else {
			return;
		};

		let atlas_size = atlas.size();

		let (instances, line_height) = layout_text(
			text.as_bytes(),
			scale,
			origin,
			gfx.window().size(),
			atlas_size,
		);

		if instances.is_empty() {
			return;
		}

		if self.submit_quads(gfx, pipelines, set, &instances, color, false) && advance_cursor {
			self.state.move_cursor_down(line_height);
		}
	}

	pub fn draw_text(
		&mut self,
		gfx: &Gfx,
		pipelines: &Pipelines,
		text: &str,
		scale: f32,
		color: u32,
	) {
		if self.atlas.is_none() {
			return;
		}

		self.state.begin_frame_if_needed(gfx.frame_number());

		let cursor = self.state.cursor();

		self.draw_text_from(gfx, pipelines, text, cursor, scale, color, true);
	}

	pub fn draw_text_at(
		&mut self,
		gfx: &Gfx,
		pipelines: &Pipelines,
		text: &str,
		position: [f32; 2],
		scale: f32,
		color: u32,
	) {
		if self.atlas.is_none() {
			return;
		}

		self.state.begin_frame_if_needed(gfx.frame_number());

		self.draw_text_from(
			gfx,
			pipelines,
			text,
			[position[0] - MARGIN[0], position[1] - MARGIN[1]],
			scale,
			color,
			false,
		);
	}

	pub fn draw_image(
		&mut self,
		gfx: &Gfx,
		pipelines: &Pipelines,
		image: &Image,
		position: [f32; 2],
		size: [f32; 2],
		color: u32,
	) {
		self.state.begin_frame_if_needed(gfx.frame_number());

		let sampler = gfx.sampler(&SamplerProps {
			address_mode: AddressMode::ClampToBorder,
			border_color: BorderColor::FloatTransparent,
			..nearest()
		});

		let entries = [
			Entry::Buffer {
				binding: 0,
				stages: VERTEX,
				buffer: self.instance_buffer.record(),
				offset: 0,
				range: u64::from(INSTANCE_SIZE) * MAX_GLYPHS as u64,
			},
			Entry::Image {
				binding: 1,
				stages: PIXEL,
				image: image.record(),
				sampler,
			},
		];

		let set = DescriptorSet::request(gfx, &entries);

		let instance = image_instance(position, size, gfx.window().size());

		self.submit_quads(gfx, pipelines, set, &[instance], color, true);
	}
}
