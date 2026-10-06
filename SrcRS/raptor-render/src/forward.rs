pub const FEATURE_NORMAL_MAP: u32 = 1 << 0;
pub const FEATURE_SKINNED: u32 = 1 << 1;
pub const FEATURE_UNLIT: u32 = 1 << 3;

pub const VERTEX_DEFAULT: u32 = 1;
pub const VERTEX_SKINNED: u32 = 2;

pub fn material_features(has_normal_or_orm: bool, skinned: bool, unlit: bool) -> u32
{
	let mut features = 0;

	if has_normal_or_orm {
		features |= FEATURE_NORMAL_MAP;
	}

	if skinned {
		features |= FEATURE_SKINNED;
	}

	if unlit {
		features |= FEATURE_UNLIT;
	}

	features
}

pub struct GeometryVariant
{
	pub features: u32,
	pub vertex_type: u32,
	pub macros: Vec<&'static str>,
	pub suffix: &'static str,
}

pub fn geometry_variant(features: u32) -> GeometryVariant
{
	let unlit = features & FEATURE_UNLIT != 0;

	let (mut variant, vertex_type, mut macros, suffix) = if features & FEATURE_SKINNED != 0 {
		(
			FEATURE_SKINNED,
			VERTEX_SKINNED,
			vec!["USE_NORMAL_MAPS", "USE_SKINNING"],
			if unlit { "SkinnedUnlit" } else { "Skinned" },
		)
	} else if !unlit && features & FEATURE_NORMAL_MAP != 0 {
		(
			FEATURE_NORMAL_MAP,
			VERTEX_DEFAULT,
			vec!["USE_NORMAL_MAPS"],
			"NormalMaps",
		)
	} else {
		(
			0,
			VERTEX_DEFAULT,
			Vec::new(),
			if unlit { "Unlit" } else { "" },
		)
	};

	if unlit {
		macros.push("UNLIT");
		variant |= FEATURE_UNLIT;
	}

	GeometryVariant {
		features: variant,
		vertex_type,
		macros,
		suffix,
	}
}

pub struct LightGrid
{
	pub columns: u32,
	pub rows: u32,
	pub capped: bool,
}

pub fn light_grid(extent: [u32; 2], tile_size: u32, max_columns: u32, max_rows: u32) -> LightGrid
{
	let columns = extent[0].div_ceil(tile_size).min(max_columns);
	let rows = extent[1].div_ceil(tile_size).min(max_rows);

	LightGrid {
		columns,
		rows,
		capped: columns * tile_size < extent[0] || rows * tile_size < extent[1],
	}
}

#[repr(C, align(16))]
pub struct SsaoBlurPush
{
	pub screen_size: [f32; 2],
	pub texel_size: [f32; 2],
	pub depth_sharpness: f32,
}

pub const SSAO_BLUR_DEPTH_SHARPNESS: f32 = 100.0;

pub fn ssao_blur_push(size: [u32; 2]) -> SsaoBlurPush
{
	let (width, height) = (size[0] as f32, size[1] as f32);

	SsaoBlurPush {
		screen_size: [width, height],
		texel_size: [1.0 / width, 1.0 / height],
		depth_sharpness: SSAO_BLUR_DEPTH_SHARPNESS,
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn plain_features_need_no_macros()
	{
		let variant = geometry_variant(0);

		assert_eq!(variant.features, 0);
		assert_eq!(variant.vertex_type, VERTEX_DEFAULT);
		assert!(variant.macros.is_empty());
		assert_eq!(variant.suffix, "");
	}

	#[test]
	fn skinning_implies_normal_maps_and_the_skinned_vertex()
	{
		let variant = geometry_variant(FEATURE_SKINNED | FEATURE_NORMAL_MAP);

		assert_eq!(variant.features, FEATURE_SKINNED);
		assert_eq!(variant.vertex_type, VERTEX_SKINNED);
		assert_eq!(variant.macros, vec!["USE_NORMAL_MAPS", "USE_SKINNING"]);
		assert_eq!(variant.suffix, "Skinned");
	}

	#[test]
	fn unlit_drops_normal_maps_and_adds_its_macro()
	{
		let variant = geometry_variant(FEATURE_UNLIT | FEATURE_NORMAL_MAP);

		assert_eq!(variant.features, FEATURE_UNLIT);
		assert_eq!(variant.macros, vec!["UNLIT"]);
		assert_eq!(variant.suffix, "Unlit");

		let skinned = geometry_variant(FEATURE_UNLIT | FEATURE_SKINNED);

		assert_eq!(skinned.features, FEATURE_SKINNED | FEATURE_UNLIT);
		assert_eq!(skinned.suffix, "SkinnedUnlit");
	}

	#[test]
	fn the_light_grid_rounds_up_and_reports_a_cap()
	{
		let grid = light_grid([900, 900], 16, 1000, 1000);

		assert_eq!((grid.columns, grid.rows, grid.capped), (57, 57, false));

		let capped = light_grid([900, 900], 16, 40, 1000);

		assert_eq!((capped.columns, capped.capped), (40, true));
	}

	#[test]
	fn the_push_constants_have_the_sizes_the_shaders_expect()
	{
		assert_eq!(std::mem::size_of::<SsaoBlurPush>(), 32);
	}

	#[test]
	fn the_blur_texel_is_the_inverse_of_the_size()
	{
		let push = ssao_blur_push([400, 200]);

		assert_eq!(push.texel_size, [1.0 / 400.0, 1.0 / 200.0]);
		assert_eq!(push.depth_sharpness, 100.0);
	}

	#[test]
	fn a_material_asks_for_the_features_it_uses()
	{
		assert_eq!(material_features(false, false, false), 0);
		assert_eq!(
			material_features(true, true, true),
			FEATURE_NORMAL_MAP | FEATURE_SKINNED | FEATURE_UNLIT
		);
	}
}
