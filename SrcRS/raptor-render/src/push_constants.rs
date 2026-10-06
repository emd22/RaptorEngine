use bytemuck::{Pod, Zeroable};

pub const DRAW_PROBE_CAPTURE: u32 = 1 << 0;
pub const DRAW_DEBUG_IRRADIANCE: u32 = 1 << 1;
pub const DRAW_DEBUG_PROBE_VISIBILITY: u32 = 1 << 2;
pub const DRAW_NO_DECALS: u32 = 1 << 3;
pub const DRAW_NO_PROBES: u32 = 1 << 4;
pub const DRAW_PROBE_BOUNCE: u32 = 1 << 5;
pub const DRAW_NO_REFLECTION_PROBES: u32 = 1 << 6;
pub const DRAW_REFLECTION_CAPTURE: u32 = 1 << 7;
pub const DRAW_DEBUG_REFLECTION: u32 = 1 << 8;
pub const DRAW_DEBUG_REFLECTION_COVERAGE: u32 = 1 << 9;

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug)]
pub struct DrawPushConstants {
	pub camera_matrix: [f32; 16],
	pub object_id: u32,
	pub material_index: u32,
	pub tile_columns: u32,
	pub flags: u32,
	pub target_size: [u32; 2],
	pub bone_base: u32,
	pub tile_rows: u32,
	pub eye_position: [f32; 4],
	pub pre_exposure: f32,
	pub padding: [f32; 3],
}

impl Default for DrawPushConstants {
	fn default() -> Self {
		Self {
			camera_matrix: [0.0; 16],
			object_id: 0,
			material_index: 0,
			tile_columns: 0,
			flags: 0,
			target_size: [0; 2],
			bone_base: 0,
			tile_rows: 0,
			eye_position: [0.0, 0.0, 0.0, 1.0],
			pre_exposure: 1.0,
			padding: [0.0; 3],
		}
	}
}

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, Default)]
pub struct DebugLayerPushConstants {
	pub combined_matrix: [f32; 16],
	pub debug_color: u32,
	pub padding: [u32; 3],
}

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, Default)]
pub struct TextPushConstants {
	pub combined_matrix: [f32; 16],
	pub text_color: u32,
	pub instance_base: u32,
	pub atlas_min_u: f32,
	pub atlas_min_v: f32,
	pub atlas_max_u: f32,
	pub atlas_max_v: f32,
	pub padding: [u32; 2],
}

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug)]
pub struct LightCullPushConstants {
	pub camera_matrix: [f32; 16],
	pub screen_size: [f32; 2],
	pub light_count: u32,
	pub tile_columns: u32,
	pub replaced_light: u32,
	pub replacement_slot: u32,
	pub decal_count: u32,
	pub padding: u32,
}

impl Default for LightCullPushConstants {
	fn default() -> Self {
		Self {
			camera_matrix: [0.0; 16],
			screen_size: [0.0; 2],
			light_count: 0,
			tile_columns: 0,
			replaced_light: u32::MAX,
			replacement_slot: 0,
			decal_count: 0,
			padding: 0,
		}
	}
}

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, Default)]
pub struct CompositionPushConstants {
	pub frame_extent: [u32; 2],
	pub padding: [u32; 2],
}

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, Default)]
pub struct SsaoPushConstants {
	pub inv_projection: [f32; 16],
	pub projection: [f32; 16],
	pub view: [f32; 16],
	pub render_size: [f32; 2],
	pub radius: f32,
	pub bias: f32,
	pub strength: f32,
	pub power: f32,
	pub floor: f32,
	pub padding: [f32; 1],
}

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, Default)]
pub struct ShadowPushConstants {
	pub camera_matrix: [f32; 16],
	pub object_index: u32,
	pub bone_base: u32,
	pub padding: [u32; 2],
}

macro_rules! pod {
	($($ty:ty),*) => {
		$(
			// SAFETY: the struct is `repr(C)` with only 4 byte numeric fields and explicit padding
			// that fills it to its alignment, so it has no uninitialised bytes.
			unsafe impl Zeroable for $ty {}
			// SAFETY: as above, and every bit pattern is a valid value of each field.
			unsafe impl Pod for $ty {}
		)*
	};
}

pod!(
	DrawPushConstants,
	DebugLayerPushConstants,
	TextPushConstants,
	LightCullPushConstants,
	CompositionPushConstants,
	SsaoPushConstants,
	ShadowPushConstants
);

const _: () = {
	assert!(size_of::<DrawPushConstants>() == 128);
	assert!(size_of::<DebugLayerPushConstants>() == 80);
	assert!(size_of::<TextPushConstants>() == 96);
	assert!(size_of::<LightCullPushConstants>() == 96);
	assert!(size_of::<CompositionPushConstants>() == 16);
	assert!(size_of::<SsaoPushConstants>() == 224);
	assert!(size_of::<ShadowPushConstants>() == 80);
};

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn the_draw_constants_put_the_eye_after_the_tile_rows() {
		assert_eq!(std::mem::offset_of!(DrawPushConstants, target_size), 80);
		assert_eq!(std::mem::offset_of!(DrawPushConstants, tile_rows), 92);
		assert_eq!(std::mem::offset_of!(DrawPushConstants, eye_position), 96);
		assert_eq!(std::mem::offset_of!(DrawPushConstants, pre_exposure), 112);
	}

	#[test]
	fn the_light_cull_constants_match_the_shader_layout() {
		assert_eq!(
			std::mem::offset_of!(LightCullPushConstants, light_count),
			72
		);
		assert_eq!(
			std::mem::offset_of!(LightCullPushConstants, decal_count),
			88
		);
	}
}
