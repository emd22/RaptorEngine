use raptor_render::shadow_atlas::{self, Region, ShadowAtlas};

pub type RxShadowAtlas = ShadowAtlas;
pub type RxShadowRegion = Region;

pub const NO_TILE: u32 = u32::MAX;

#[unsafe(no_mangle)]
pub extern "C" fn rx_shadow_atlas_width() -> u32
{
	shadow_atlas::WIDTH
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_shadow_atlas_height() -> u32
{
	shadow_atlas::HEIGHT
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_shadow_atlas_directional_size() -> u32
{
	shadow_atlas::DIRECTIONAL_SIZE
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_shadow_atlas_spot_tile_size() -> u32
{
	shadow_atlas::SPOT_TILE_SIZE
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_shadow_atlas_max_spot_tiles() -> u32
{
	shadow_atlas::MAX_SPOT_TILES
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_shadow_atlas_new() -> *mut RxShadowAtlas
{
	Box::into_raw(Box::new(ShadowAtlas::default()))
}

/// # Safety
///
/// `atlas` must be null or come from `rx_shadow_atlas_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_free(atlas: *mut RxShadowAtlas)
{
	if !atlas.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(atlas) });
	}
}

/// Takes the first free spot light tile, or returns `u32::MAX` if every tile is in use.
///
/// # Safety
///
/// `atlas` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_allocate_spot_tile(atlas: *mut RxShadowAtlas) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *atlas }
		.allocate_spot_tile()
		.unwrap_or(NO_TILE)
}

/// # Safety
///
/// `atlas` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_free_spot_tile(atlas: *mut RxShadowAtlas, tile: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *atlas }.free_spot_tile(tile);
}

/// # Safety
///
/// `atlas` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_invalidate(atlas: *mut RxShadowAtlas)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *atlas }.invalidate();
}

/// # Safety
///
/// `atlas` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_generation(atlas: *const RxShadowAtlas) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*atlas }.generation()
}

/// # Safety
///
/// `atlas` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_is_initialized(atlas: *const RxShadowAtlas) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*atlas }.is_initialized())
}

/// Starts a pass over `region` and writes the area its render pass should clear to
/// `out_render_area`.
///
/// # Safety
///
/// `atlas` must be live, `region` readable and `out_render_area` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_begin_region(
	atlas: *mut RxShadowAtlas,
	region: *const RxShadowRegion,
	out_render_area: *mut RxShadowRegion,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { *out_render_area = (*atlas).begin_region(*region) };
}

/// # Safety
///
/// `out` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_directional_region(out: *mut RxShadowRegion)
{
	// SAFETY: guaranteed by the caller.
	unsafe { *out = shadow_atlas::directional_region() };
}

/// Writes the region of a spot light tile and returns 1, or returns 0 if there is no such tile.
///
/// # Safety
///
/// `out` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_spot_tile_region(tile: u32, out: *mut RxShadowRegion)
-> u8
{
	match shadow_atlas::spot_tile_region(tile) {
		Some(region) => {
			// SAFETY: guaranteed by the caller.
			unsafe { *out = region };
			1
		}
		None => 0,
	}
}

/// # Safety
///
/// `region` must be readable and `out` valid for 4 floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_shadow_atlas_region_uv_transform(
	region: *const RxShadowRegion,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let transform = shadow_atlas::region_uv_transform(unsafe { *region });

	// SAFETY: guaranteed by the caller.
	unsafe { std::ptr::copy_nonoverlapping(transform.as_ptr(), out, 4) };
}
