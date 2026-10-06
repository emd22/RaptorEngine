use ash::vk;
use raptor_core::log_warn;
use raptor_entity::CameraCore;
use raptor_gfx::{Gfx, GpuCore};
use raptor_gpu::VertexType;
use raptor_math::mat4;
use std::sync::Arc;

use crate::debug_draw::{DebugDraw, MAX_SHAPES, Queued, Shape};
use crate::names::PipelineName;
use crate::pipelines::Pipelines;
use crate::primitive_mesh::PrimitiveMesh;
use crate::push_constants::DebugLayerPushConstants;

pub struct DebugRenderer {
	pub queue: DebugDraw,
	meshes: [Option<PrimitiveMesh>; 3],
}

impl Default for DebugRenderer {
	fn default() -> Self {
		Self::new()
	}
}

fn pipeline_for(shape: Shape) -> PipelineName {
	if shape == Shape::SolidBox {
		PipelineName::DebugSolid
	} else {
		PipelineName::DebugLayer
	}
}

impl DebugRenderer {
	pub fn new() -> Self {
		Self {
			queue: DebugDraw::default(),
			meshes: [None, None, None],
		}
	}

	pub fn report(queued: Queued) {
		if queued == Queued::DroppedFirst {
			log_warn!(
				"Debug draw queue is full ({MAX_SHAPES} shapes), dropping the rest until it is drawn"
			);
		}
	}

	pub fn line(&mut self, from: [f32; 3], to: [f32; 3], color: u32) {
		Self::report(self.queue.line(from, to, color));
	}

	pub fn wire_box(&mut self, world: mat4::Mat4, color: u32) {
		Self::report(self.queue.draw(Shape::WireBox, world, color));
	}

	pub fn solid_box(&mut self, world: mat4::Mat4, color: u32) {
		Self::report(self.queue.draw(Shape::SolidBox, world, color));
	}

	pub fn wire_aabb(&mut self, min: [f32; 3], max: [f32; 3], color: u32) {
		let (center, extent) = crate::debug_draw::aabb_center_and_extent(min, max);

		self.wire_box(
			crate::debug_draw::box_matrix(center, extent, mat4::Quat::IDENTITY),
			color,
		);
	}

	pub fn solid_aabb(&mut self, min: [f32; 3], max: [f32; 3], color: u32) {
		let (center, extent) = crate::debug_draw::aabb_center_and_extent(min, max);

		self.solid_box(
			crate::debug_draw::box_matrix(center, extent, mat4::Quat::IDENTITY),
			color,
		);
	}

	fn mesh(&mut self, gfx: &Gfx, core: &Arc<GpuCore>, shape: Shape) -> &PrimitiveMesh {
		self.meshes[shape as usize].get_or_insert_with(|| {
			let source = match shape {
				Shape::Line => raptor_mesh::line(),
				Shape::WireBox => raptor_mesh::wireframe_box(),
				Shape::SolidBox => raptor_mesh::cube(&raptor_mesh::CubeOptions::default()),
			};

			let mut mesh = PrimitiveMesh::from_mesh(core, &source, VertexType::Slim);

			if let Err(error) = mesh.upload_now(gfx) {
				raptor_core::log_error!(Render; "Could not upload a debug shape: {error:?}");
			}

			mesh
		})
	}

	pub fn render(&mut self, gfx: &Gfx, pipelines: &Pipelines, camera: &CameraCore) {
		if self.queue.queued_count() > 0 {
			let camera_matrix = mat4::unflatten(&camera.camera_matrix);
			let commands = self.queue.commands(&camera_matrix).to_vec();

			let cmd = gfx.frame_cmd();
			let core = Arc::clone(gfx.core());

			let mut bound_shape = u32::MAX;

			for command in commands {
				let Some(shape) = Shape::from_raw(command.shape) else {
					continue;
				};

				if command.shape != bound_shape {
					pipelines.bind_named(gfx, cmd, pipeline_for(shape));
					bound_shape = command.shape;
				}

				let Some(slot) = pipelines.slot(gfx, pipeline_for(shape).handle()) else {
					continue;
				};

				let push = DebugLayerPushConstants {
					combined_matrix: command.combined_matrix,
					debug_color: command.color,
					padding: [0; 3],
				};

				pipelines.push_constants(
					gfx,
					cmd,
					slot,
					vk::ShaderStageFlags::VERTEX,
					bytemuck::bytes_of(&push),
				);

				self.mesh(gfx, &core, shape).render(gfx, cmd, 1);
			}
		}

		self.queue.clear();
	}

	pub fn destroy(&mut self) {
		self.queue.clear();
		self.meshes = [None, None, None];
	}
}
