pub const NO_ID: u32 = u32::MAX;
pub const NO_TILE: u32 = u32::MAX;

pub const TYPE_UNKNOWN: u32 = 0;
pub const TYPE_DIRECTIONAL: u32 = 1;
pub const TYPE_POINT: u32 = 2;
pub const TYPE_SPOT: u32 = 3;

/// What a light is: its colour, strength, reach and cone, with the state of its baked shadow. The
/// engine reads this straight out of the record, so the layout is fixed: it is also described in
/// the C header.
#[repr(C, align(16))]
#[derive(Clone, Debug)]
pub struct LightCore
{
	pub shadow_matrix: [f32; 16],
	pub shadow_atlas_tile: u32,
	pub shadow_bake_hash: u32,
	shadow_padding: [u32; 2],
	pub color: u32,
	pub intensity: f32,
	pub radius: f32,
	pub inner_angle: f32,
	pub outer_angle: f32,
	pub light_id: u32,
	pub light_type: u32,
	pub flags: u16,
	pub enabled: u8,
	pub cast_shadows: u8,
}

impl Default for LightCore
{
	fn default() -> Self
	{
		Self {
			shadow_matrix: raptor_math::Mat4f::identity().to_rows(),
			shadow_atlas_tile: NO_TILE,
			shadow_bake_hash: 0,
			shadow_padding: [0; 2],
			color: 0xFFFF_FFFF,
			intensity: 100_000.0,
			radius: 1.0,
			inner_angle: 20.0f32.to_radians(),
			outer_angle: 30.0f32.to_radians(),
			light_id: NO_ID,
			light_type: TYPE_UNKNOWN,
			flags: 0,
			enabled: 1,
			cast_shadows: 1,
		}
	}
}

impl LightCore
{
	pub fn is_cullable(&self) -> bool
	{
		self.light_type != TYPE_DIRECTIONAL
	}

	pub fn reset_shadow(&mut self)
	{
		self.shadow_matrix = raptor_math::Mat4f::identity().to_rows();
		self.shadow_atlas_tile = NO_TILE;
		self.shadow_bake_hash = 0;
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn layout_is_fixed()
	{
		assert_eq!(std::mem::offset_of!(LightCore, shadow_atlas_tile), 64);
		assert_eq!(std::mem::offset_of!(LightCore, color), 80);
		assert_eq!(std::mem::offset_of!(LightCore, light_id), 100);
		assert_eq!(std::mem::offset_of!(LightCore, flags), 108);
		assert_eq!(std::mem::size_of::<LightCore>(), 112);
	}

	#[test]
	fn resetting_the_shadow_leaves_the_light_alone()
	{
		let mut core = LightCore {
			shadow_atlas_tile: 3,
			shadow_bake_hash: 9,
			radius: 5.0,
			..LightCore::default()
		};

		core.reset_shadow();

		assert_eq!(core.shadow_atlas_tile, NO_TILE);
		assert_eq!(core.shadow_bake_hash, 0);
		assert_eq!(core.radius, 5.0);
	}

	#[test]
	fn only_directional_lights_are_not_cullable()
	{
		let mut core = LightCore::default();

		assert!(core.is_cullable());

		core.light_type = TYPE_DIRECTIONAL;

		assert!(!core.is_cullable());
	}
}
