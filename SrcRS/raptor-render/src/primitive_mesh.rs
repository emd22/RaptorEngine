use std::sync::Arc;

use ash::vk::{self, Handle};
use raptor_gfx::{CommandBuffer, Gfx, GpuBuffer, GpuCore, Uploader};
use raptor_gpu::{BufferType, Memory, VertexType};

use crate::mesh_pack::Attributes;
use crate::mesh_record::{MeshRecord, NormalsStatus};

pub struct PrimitiveMesh {
	pub record: MeshRecord,
	pub vertex_buffer: GpuBuffer,
	pub index_buffer: GpuBuffer,
}

fn flatten3(values: &[[f32; 3]]) -> Vec<f32> {
	values.iter().flatten().copied().collect()
}

fn flatten2(values: &[[f32; 2]]) -> Vec<f32> {
	values.iter().flatten().copied().collect()
}

pub fn upload_to_gpu_buffer(
	uploader: &Uploader,
	target: &GpuBuffer,
	buffer_type: BufferType,
	data: &[u8],
) -> Result<(), vk::Result> {
	if data.is_empty() {
		return Ok(());
	}

	let staging = uploader.staging(data)?;

	target.create(
		buffer_type,
		data.len() as u64,
		Memory::GpuOnly,
		raptor_gfx::buffer::FLAG_TRANSFER_RECEIVER,
	)?;

	// SAFETY: the command buffer is recording and both buffers are live with transfer usage.
	unsafe {
		uploader.core.device().cmd_copy_buffer(
			uploader.cmd,
			vk::Buffer::from_raw(staging.fields().buffer),
			target.handle(),
			data.len() as u64,
		)
	};

	uploader.retire_staging(staging);

	Ok(())
}

impl PrimitiveMesh {
	pub fn new(core: &Arc<GpuCore>) -> Self {
		Self {
			record: MeshRecord::default(),
			vertex_buffer: GpuBuffer::new(core),
			index_buffer: GpuBuffer::new(core),
		}
	}

	pub fn from_mesh(
		core: &Arc<GpuCore>,
		mesh: &raptor_mesh::Mesh,
		vertex_type: VertexType,
	) -> Self {
		let mut primitive = Self::new(core);

		let positions = flatten3(&mesh.positions);
		let normals = flatten3(&mesh.normals);
		let uvs = flatten2(&mesh.texcoords);
		let tangents = flatten3(&mesh.tangents);

		let packed = match vertex_type {
			VertexType::Slim => primitive.record.pack(&Attributes {
				positions: &positions,
				..Attributes::default()
			}),
			_ => primitive.record.pack(&Attributes {
				positions: &positions,
				normals: &normals,
				uvs: &uvs,
				tangents: &tangents,
				tangent_stride: 3,
				handedness: 1.0,
				..Attributes::default()
			}),
		};

		if !packed {
			raptor_core::log_error!(Asset; "The vertex attributes of a generated mesh are not valid");
		}

		primitive.record.set_indices(&mesh.indices);

		primitive
	}

	pub fn is_ready(&self) -> bool {
		self.record.is_ready()
	}

	pub fn upload_vertices(&mut self, uploader: &Uploader) -> Result<(), vk::Result> {
		match self.record.ensure_normals() {
			NormalsStatus::Recalculated => raptor_core::log_debug!("Calculated normals for mesh"),
			NormalsStatus::MissingIndices => {
				raptor_core::log_warn!(Asset; "Cannot recalculate normals as local indices are missing!")
			}
			NormalsStatus::MissingVertices => {
				raptor_core::log_warn!(Asset; "Cannot recalculate normals as local vertices are missing!")
			}
			NormalsStatus::IndicesDoNotFit => {
				raptor_core::log_warn!(Asset; "Cannot recalculate normals as the indices do not fit the vertices!")
			}
			NormalsStatus::Present => {}
		}

		upload_to_gpu_buffer(
			uploader,
			&self.vertex_buffer,
			BufferType::VertexBuffer,
			self.record.vertex_bytes(),
		)
	}

	pub fn upload_indices(&mut self, uploader: &Uploader) -> Result<(), vk::Result> {
		upload_to_gpu_buffer(
			uploader,
			&self.index_buffer,
			BufferType::IndexBuffer,
			index_bytes(self.record.indices()),
		)
	}

	pub fn upload_now(&mut self, gfx: &Gfx) -> Result<(), vk::Result> {
		let family = gfx.upload().family();

		let mut result = Ok(());

		gfx.submit_immediate_upload(|cmd| {
			let uploader = Uploader::new(gfx.core(), cmd, family);

			result = self
				.upload_vertices(&uploader)
				.and_then(|()| self.upload_indices(&uploader));
		})?;

		self.record.set_ready(result.is_ok());

		result
	}

	pub fn render(&self, gfx: &Gfx, cmd: &CommandBuffer, instances: u32) {
		let device = gfx.device().raw();

		let index_count = (self.index_buffer.size() / 4) as u32;

		// SAFETY: the command buffer is recording inside a render pass with a pipeline bound, and
		// both buffers are live.
		unsafe {
			device.cmd_bind_vertex_buffers(cmd.raw(), 0, &[self.vertex_buffer.handle()], &[0]);
			device.cmd_bind_index_buffer(
				cmd.raw(),
				self.index_buffer.handle(),
				0,
				vk::IndexType::UINT32,
			);
			device.cmd_draw_indexed(cmd.raw(), index_count, instances, 0, 0, 0);
		}
	}

	pub fn destroy(&mut self) {
		if self.record.is_reference || !self.record.is_ready() {
			return;
		}

		self.record.set_ready(false);
		self.vertex_buffer.destroy();
		self.index_buffer.destroy();
		self.record.clear_local();
	}
}

fn index_bytes(indices: &[u32]) -> &[u8] {
	bytemuck::cast_slice(indices)
}

impl Drop for PrimitiveMesh {
	fn drop(&mut self) {
		self.destroy();
	}
}
