mod bounds;
mod clip;
mod create;
mod face;
mod grab;
mod light;
mod rotate;
mod translate;

pub use bounds::BoundsTool;
pub use clip::ClipTool;
pub use create::CreateTool;
pub use face::FaceTool;
pub use grab::GrabTool;
pub use light::LightTool;
pub use rotate::RotateTool;
pub use translate::TranslateTool;

use crate::core::EditorCore;
use crate::host::EditorHost;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum EditorTool {
	None,
	Translate,
	Face,
	Rotate,
	Create,
	Clip,
	Light,
	Bounds,
	Grab,
}

impl EditorTool {
	pub const COUNT: usize = 9;

	pub const ALL: [EditorTool; Self::COUNT] = [
		EditorTool::None,
		EditorTool::Translate,
		EditorTool::Face,
		EditorTool::Rotate,
		EditorTool::Create,
		EditorTool::Clip,
		EditorTool::Light,
		EditorTool::Bounds,
		EditorTool::Grab,
	];

	pub fn index(self) -> usize {
		self as usize
	}

	pub fn from_index(index: usize) -> Option<EditorTool> {
		Self::ALL.get(index).copied()
	}

	pub fn name(self) -> &'static str {
		match self {
			EditorTool::None => "Simulate",
			EditorTool::Translate => "Transform",
			EditorTool::Face => "Face",
			EditorTool::Rotate => "Rotate",
			EditorTool::Create => "Create",
			EditorTool::Clip => "Clip",
			EditorTool::Light => "Light",
			EditorTool::Bounds => "Bounds",
			EditorTool::Grab => "Grab",
		}
	}

	pub fn icon(self) -> &'static str {
		match self {
			EditorTool::None => "Textures/editor/simulate.png",
			EditorTool::Translate => "Textures/editor/move.png",
			EditorTool::Face => "Textures/editor/face.png",
			EditorTool::Rotate => "Textures/editor/rotate.png",
			EditorTool::Create => "Textures/editor/create.png",
			EditorTool::Clip => "Textures/editor/clip.png",
			EditorTool::Light => "Textures/editor/lamp.png",
			EditorTool::Bounds => "Textures/editor/bounds.png",
			EditorTool::Grab => "Textures/editor/grab.png",
		}
	}

	pub fn flags(self) -> ToolFlags {
		match self {
			EditorTool::None | EditorTool::Create | EditorTool::Grab => ToolFlags::NONE,
			EditorTool::Translate | EditorTool::Rotate | EditorTool::Bounds => ToolFlags {
				uses_selection: true,
				uses_models: true,
				clears_selection: false,
			},
			EditorTool::Face | EditorTool::Clip => ToolFlags {
				uses_selection: true,
				uses_models: false,
				clears_selection: false,
			},
			EditorTool::Light => ToolFlags {
				uses_selection: false,
				uses_models: false,
				clears_selection: true,
			},
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolFlags {
	pub uses_selection: bool,
	pub clears_selection: bool,
	pub uses_models: bool,
}

impl ToolFlags {
	pub const NONE: ToolFlags = ToolFlags {
		uses_selection: false,
		clears_selection: false,
		uses_models: false,
	};
}

pub struct ToolCtx<'a> {
	pub host: &'a mut dyn EditorHost,
	pub core: &'a mut EditorCore,
}

pub trait Tool {
	fn enter(&mut self, _ctx: &mut ToolCtx) {}
	fn leave(&mut self, _ctx: &mut ToolCtx) {}
	fn begin(&mut self, _ctx: &mut ToolCtx) {}
	fn update(&mut self, _ctx: &mut ToolCtx, _delta_time: f32) {}
	fn finalize(&mut self, _ctx: &mut ToolCtx) {}
	fn cancel(&mut self, _ctx: &mut ToolCtx) {}
	fn controls(&mut self, _ctx: &mut ToolCtx) {}
}

pub const TRANSFORM_EPSILON: f32 = 1e-5;
