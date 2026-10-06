use raptor_math::Mat4f;
use raptor_render::world_logic::{
	CasterState, clamp_tile_range, far_to_near_order, skinned_caster_reaches_sphere,
	spot_bake_hash, visible_tiles,
};

use crate::world_grid::RxWorldGrid;

/// Writes the indices of `count` objects with the furthest from `camera` first.
///
/// # Safety
///
/// `centers` must hold three floats per object, `camera` three, and `out` room for `count` values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_far_to_near(
	centers: *const f32,
	count: usize,
	camera: *const f32,
	out: *mut u32,
)
{
	if count == 0 {
		return;
	}

	// SAFETY: guaranteed by the caller.
	let (centers, camera, out) = unsafe {
		(
			std::slice::from_raw_parts(centers, count * 3),
			std::slice::from_raw_parts(camera, 3),
			std::slice::from_raw_parts_mut(out, count),
		)
	};

	out.copy_from_slice(&far_to_near_order(
		centers,
		[camera[0], camera[1], camera[2]],
	));
}

/// Clamps the corners of a tile range to a grid of `width` by `height` tiles.
///
/// # Safety
///
/// Each of `min` and `max` must hold two values and be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_clamp_tile_range(
	min: *mut u32,
	max: *mut u32,
	width: u32,
	height: u32,
)
{
	// SAFETY: guaranteed by the caller.
	let (min, max) = unsafe {
		(
			std::slice::from_raw_parts_mut(min, 2),
			std::slice::from_raw_parts_mut(max, 2),
		)
	};

	let (low, high) = clamp_tile_range([min[0], min[1]], [max[0], max[1]], [width, height]);

	min.copy_from_slice(&low);
	max.copy_from_slice(&high);
}

/// Writes the tiles that could be in view of a camera, up to `capacity`, and returns how many there
/// are. The matrix is 16 floats in row order.
///
/// # Safety
///
/// `grid` must be live, `view_projection` hold sixteen floats and `out` have room for `capacity`
/// tiles.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_visible_tiles(
	grid: *const RxWorldGrid,
	view_projection: *const f32,
	out: *mut u32,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let tiles = unsafe {
		visible_tiles(
			&*grid,
			&Mat4f::from_rows(&*view_projection.cast::<[f32; 16]>()),
		)
	};

	for (index, tile) in tiles.iter().take(capacity).enumerate() {
		// SAFETY: guaranteed by the caller.
		unsafe { *out.add(index) = *tile };
	}

	tiles.len()
}

#[repr(C)]
pub struct RxCasterState
{
	pub id: u32,
	pub drawable: bool,
	pub has_pose: bool,
	pub has_bones: bool,
	pub pose_hash: u32,
	pub mesh: u64,
	pub world_matrix: [f32; 16],
}

/// A hash of what a spot light's baked shadow depends on.
///
/// # Safety
///
/// `shadow_matrix` holds sixteen floats and `casters` holds `caster_count` casters.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_spot_bake_hash(
	atlas_generation: u32,
	tile: u32,
	shadow_matrix: *const f32,
	casters: *const RxCasterState,
	caster_count: usize,
) -> u32
{
	let casters: Vec<CasterState> = if caster_count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(casters, caster_count) }
			.iter()
			.map(|caster| CasterState {
				id: caster.id,
				mesh: caster.mesh,
				drawable: caster.drawable,
				world_matrix: caster.world_matrix,
				pose: caster
					.has_pose
					.then_some((caster.pose_hash, caster.has_bones)),
			})
			.collect()
	};

	// SAFETY: guaranteed by the caller.
	spot_bake_hash(
		atlas_generation,
		tile,
		unsafe { &*shadow_matrix.cast::<[f32; 16]>() },
		&casters,
	)
}

/// Whether a skinned object reaches a spot light's sphere.
///
/// # Safety
///
/// The matrix holds sixteen floats and the vectors three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_skinned_reaches_sphere(
	world_matrix: *const f32,
	scale: f32,
	pose_center: *const f32,
	pose_radius: f32,
	padding: f32,
	center: *const f32,
	radius: f32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	let (matrix, pose_center, center) = unsafe {
		(
			&*world_matrix.cast::<[f32; 16]>(),
			std::slice::from_raw_parts(pose_center, 3),
			std::slice::from_raw_parts(center, 3),
		)
	};

	skinned_caster_reaches_sphere(
		matrix,
		scale,
		[pose_center[0], pose_center[1], pose_center[2]],
		pose_radius,
		padding,
		[center[0], center[1], center[2]],
		radius,
	)
}
