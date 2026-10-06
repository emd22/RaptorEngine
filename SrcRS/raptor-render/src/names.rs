use raptor_gpu::INVALID_INDEX;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[repr(u16)]
pub enum PipelineName {
	DebugLayer,
	DebugSolid,
	LightCulling,
	TextRendering,
	ImageRendering,
	Composition,
	CompositionAgx,
	Ssao,
	SsaoBlur,
}

pub const NUM_PIPELINES: u32 = 9;

impl PipelineName {
	pub const ALL: [PipelineName; NUM_PIPELINES as usize] = [
		PipelineName::DebugLayer,
		PipelineName::DebugSolid,
		PipelineName::LightCulling,
		PipelineName::TextRendering,
		PipelineName::ImageRendering,
		PipelineName::Composition,
		PipelineName::CompositionAgx,
		PipelineName::Ssao,
		PipelineName::SsaoBlur,
	];

	pub fn handle(self) -> PipelineHandle {
		PipelineHandle(self as u32)
	}

	pub fn name(self) -> &'static str {
		match self {
			PipelineName::DebugLayer => "DebugLayer",
			PipelineName::DebugSolid => "DebugSolid",
			PipelineName::LightCulling => "LightCulling",
			PipelineName::TextRendering => "TextRendering",
			PipelineName::ImageRendering => "ImageRendering",
			PipelineName::Composition => "Composition",
			PipelineName::CompositionAgx => "CompositionAgx",
			PipelineName::Ssao => "SSAO",
			PipelineName::SsaoBlur => "SSAOBlur",
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum ShaderName {
	Geometry,
	Lighting,
	Forward,
	LightCulling,
	Composition,
	Shadows,
	DepthNormal,
	DebugLayer,
	BitmapText,
	Ssao,
	SsaoBlur,
}

impl ShaderName {
	pub fn name(self) -> &'static str {
		match self {
			ShaderName::Geometry => "Geometry",
			ShaderName::Lighting => "Lighting",
			ShaderName::Forward => "Forward",
			ShaderName::LightCulling => "LightCulling",
			ShaderName::Composition => "Composition",
			ShaderName::Shadows => "Shadows",
			ShaderName::DepthNormal => "DepthNormal",
			ShaderName::DebugLayer => "DebugLayer",
			ShaderName::BitmapText => "BitmapText",
			ShaderName::Ssao => "SSAO",
			ShaderName::SsaoBlur => "SSAOBlur",
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct PipelineHandle(pub u32);

impl PipelineHandle {
	pub const INVALID: PipelineHandle = PipelineHandle(INVALID_INDEX);

	pub fn is_valid(self) -> bool {
		self.0 != INVALID_INDEX
	}
}

impl Default for PipelineHandle {
	fn default() -> Self {
		Self::INVALID
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum PipelinePass {
	Depth,
	Forward,
	ForwardBlend,
	ForwardCapture,
	ForwardDebug,
	ForwardBlendDebug,
	Shadow,
}

pub const NUM_PASSES: u32 = 7;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct Features(pub u32);

impl Features {
	pub const NONE: Features = Features(0);
	pub const NORMAL_MAP: Features = Features(1 << 0);
	pub const SKINNED: Features = Features(1 << 1);
	pub const ALPHA_MASK: Features = Features(1 << 2);
	pub const UNLIT: Features = Features(1 << 3);

	pub fn contains(self, other: Features) -> bool {
		self.0 & other.0 == other.0 && other.0 != 0
	}
}

impl std::ops::BitOr for Features {
	type Output = Features;

	fn bitor(self, rhs: Features) -> Features {
		Features(self.0 | rhs.0)
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_fixed_pipelines_are_handles_in_declaration_order() {
		assert_eq!(PipelineName::LightCulling.handle(), PipelineHandle(2));
		assert_eq!(PipelineName::ALL.len() as u32, NUM_PIPELINES);
		assert_eq!(PipelineName::SsaoBlur as u32 + 1, NUM_PIPELINES);
		assert!(!PipelineHandle::default().is_valid());
	}

	#[test]
	fn features_combine_and_are_tested_by_containment() {
		let features = Features::SKINNED | Features::NORMAL_MAP;

		assert!(features.contains(Features::SKINNED));
		assert!(!features.contains(Features::UNLIT));
		assert!(!Features::NONE.contains(Features::NONE));
	}
}
