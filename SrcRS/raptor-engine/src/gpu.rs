use std::sync::Arc;

use raptor_asset::manager::AssetManager;
use raptor_asset::material_library::MaterialLibrary;
use raptor_asset::materials::{MaterialGpu, MaterialId, MaterialStore};
use raptor_gfx::Gfx;
use raptor_render::names::{Features, PipelineHandle, PipelinePass};
use raptor_render::pipelines::Pipelines;
use raptor_render::render_system::RenderSystem;
use raptor_render::renderer::Renderer;

use crate::meshes::MeshStore;
use crate::object_buffer::ObjectBuffer;

/// Everything the engine keeps on the GPU side: the renderer and the stores the objects draw from.
pub struct Gpu
{
	pub gfx: Arc<Gfx>,
	pub render: RenderSystem,
	pub assets: Arc<AssetManager>,
	pub materials: Arc<MaterialStore>,
	pub meshes: Arc<MeshStore>,
	pub objects: ObjectBuffer,
	pub library: MaterialLibrary,
}

/// The read-only parts of the GPU side that drawing an object needs.
#[derive(Clone, Copy)]
pub struct Draw<'a>
{
	pub gfx: &'a Gfx,
	pub pipelines: &'a Pipelines,
	pub renderer: &'a Renderer,
	pub materials: &'a MaterialStore,
	pub meshes: &'a MeshStore,
	pub objects: &'a ObjectBuffer,
	pub assets: &'a AssetManager,
}

impl<'a> Draw<'a>
{
	pub fn material_gpu(&self) -> MaterialGpu<'a>
	{
		MaterialGpu {
			gfx: self.gfx,
			assets: self.assets,
		}
	}

	pub fn pipeline_for_material(&self, material: MaterialId) -> PipelineHandle
	{
		let record = self
			.materials
			.get(material)
			.unwrap_or_else(|| self.materials.null_material());

		let pass = if record.is_transparent() {
			PipelinePass::ForwardBlend
		} else {
			PipelinePass::Forward
		};

		self.pipelines
			.get_or_create_variant(self.gfx, pass, Features(record.pipeline_features()))
	}

	pub fn shadow_list_pipeline(&self) -> PipelineHandle
	{
		self.pipelines
			.get_or_create_variant(self.gfx, PipelinePass::Shadow, Features::NONE)
	}
}

impl Gpu
{
	pub fn draw(&self) -> Draw<'_>
	{
		Draw {
			gfx: &self.gfx,
			pipelines: &self.render.pipelines,
			renderer: &self.render.renderer,
			materials: &self.materials,
			meshes: &self.meshes,
			objects: &self.objects,
			assets: &self.assets,
		}
	}
}
