use std::slice;

use raptor_math::{Aabb, Mat4f, Vec3f};
use raptor_render::probe_placement::{
	self, Fill, Placement, PlacementBox, PlacementBoxes, Plane, TooManyProbes,
};

pub type RxPlacementBoxes = PlacementBoxes;
pub type RxPlacement = Placement;

pub const RX_PLACEMENT_NO_PROBE: u32 = u32::MAX;

fn vec3(values: *const f32) -> Vec3f
{
	// SAFETY: callers pass three floats.
	let values = unsafe { slice::from_raw_parts(values, 3) };

	Vec3f::new(values[0], values[1], values[2])
}

fn dims(values: *const u32) -> [u32; 3]
{
	// SAFETY: callers pass three values.
	let values = unsafe { slice::from_raw_parts(values, 3) };

	[values[0], values[1], values[2]]
}

fn write_vec3(out: *mut f32, value: Vec3f)
{
	// SAFETY: callers pass room for three floats.
	unsafe { slice::from_raw_parts_mut(out, 3) }.copy_from_slice(&value.to_array());
}

fn fill_from(fill: u32) -> Fill
{
	if fill == 0 {
		Fill::Dense
	} else {
		Fill::Surface
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_placement_boxes_new() -> *mut RxPlacementBoxes
{
	Box::into_raw(Box::new(PlacementBoxes::default()))
}

/// # Safety
///
/// `boxes` must be null or come from `rx_placement_boxes_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_boxes_free(boxes: *mut RxPlacementBoxes)
{
	if !boxes.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(boxes) });
	}
}

/// Adds a box and returns whether there was room for it. The matrices are 16 floats in row order,
/// and `planes` holds `plane_count` normals and distances as four floats each.
///
/// # Safety
///
/// `boxes` must be live, the vectors must hold three floats, the matrices sixteen, and `planes`
/// must hold `plane_count` groups of four floats if it is not zero.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_boxes_add(
	boxes: *mut RxPlacementBoxes,
	local_min: *const f32,
	local_max: *const f32,
	world_min: *const f32,
	world_max: *const f32,
	local_to_world: *const f32,
	world_to_local: *const f32,
	axis_aligned: bool,
	planes: *const f32,
	plane_count: u32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	let boxes = unsafe { &mut *boxes };

	// SAFETY: guaranteed by the caller.
	let (local_to_world, world_to_local) = unsafe {
		(
			Mat4f::from_rows(&*(local_to_world as *const [f32; 16])),
			Mat4f::from_rows(&*(world_to_local as *const [f32; 16])),
		)
	};

	let planes = if plane_count == 0 {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { slice::from_raw_parts(planes, plane_count as usize * 4) }
			.as_chunks::<4>()
			.0
			.iter()
			.map(|plane| Plane {
				normal: Vec3f::new(plane[0], plane[1], plane[2]),
				distance: plane[3],
			})
			.collect()
	};

	boxes.add(PlacementBox {
		local_bounds: Aabb::new(vec3(local_min), vec3(local_max)),
		world_bounds: Aabb::new(vec3(world_min), vec3(world_max)),
		axis_aligned,
		local_to_world,
		world_to_local,
		planes,
	})
}

/// # Safety
///
/// `boxes` must be live and the vectors must hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_boxes_extend(
	boxes: *mut RxPlacementBoxes,
	min: *const f32,
	max: *const f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *boxes }.extend_bounds(vec3(min), vec3(max));
}

/// # Safety
///
/// `boxes` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_boxes_count(boxes: *const RxPlacementBoxes) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*boxes }.len() as u32
}

/// # Safety
///
/// `boxes` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_boxes_is_empty(boxes: *const RxPlacementBoxes) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*boxes }.is_empty()
}

/// # Safety
///
/// `boxes` must be live and the outputs must have room for three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_boxes_bounds(
	boxes: *const RxPlacementBoxes,
	out_min: *mut f32,
	out_max: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let boxes = unsafe { &*boxes };

	write_vec3(out_min, boxes.min());
	write_vec3(out_max, boxes.max());
}

/// How many of the grid's points get a probe, with `fill` 0 for dense and 1 for surface.
///
/// # Safety
///
/// `boxes` must be live, and the vectors and dims must hold three values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_count_needed(
	boxes: *const RxPlacementBoxes,
	volume_min: *const f32,
	cell_size: *const f32,
	grid_dims: *const u32,
	fill: u32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	let boxes = unsafe { &*boxes };

	probe_placement::find_needed_grid_points(
		vec3(volume_min),
		vec3(cell_size),
		dims(grid_dims),
		boxes,
		fill_from(fill),
	)
	.0
}

/// Places the probes of a volume. Returns null if more than `probe_budget` are needed.
///
/// # Safety
///
/// `boxes` must be live, and the vectors and dims must hold three values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_place_volume(
	boxes: *const RxPlacementBoxes,
	volume_min: *const f32,
	volume_size: *const f32,
	grid_dims: *const u32,
	fill: u32,
	probe_budget: u32,
) -> *mut RxPlacement
{
	// SAFETY: guaranteed by the caller.
	let boxes = unsafe { &*boxes };

	match probe_placement::place_volume(
		boxes,
		vec3(volume_min),
		vec3(volume_size),
		dims(grid_dims),
		fill_from(fill),
		probe_budget,
	) {
		Ok(placement) => Box::into_raw(Box::new(placement)),
		Err(TooManyProbes) => std::ptr::null_mut(),
	}
}

/// # Safety
///
/// `placement` must be null or come from `rx_probe_place_volume`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_free(placement: *mut RxPlacement)
{
	if !placement.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(placement) });
	}
}

/// Writes the counts: needed, pushed, hugged, relocated, unplaced and placed.
///
/// # Safety
///
/// `placement` must be live and `out` must have room for six values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_counts(placement: *const RxPlacement, out: *mut u32)
{
	// SAFETY: guaranteed by the caller.
	let placement = unsafe { &*placement };

	// SAFETY: guaranteed by the caller.
	unsafe { slice::from_raw_parts_mut(out, 6) }.copy_from_slice(&[
		placement.needed,
		placement.pushed,
		placement.hugged,
		placement.relocated,
		placement.unplaced,
		placement.positions.len() as u32,
	]);
}

/// Writes the position of each placed probe as three floats.
///
/// # Safety
///
/// `placement` must be live and `out` must have room for three floats per placed probe.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_positions(placement: *const RxPlacement, out: *mut f32)
{
	// SAFETY: guaranteed by the caller.
	let placement = unsafe { &*placement };

	// SAFETY: guaranteed by the caller.
	let out = unsafe { slice::from_raw_parts_mut(out, placement.positions.len() * 3) };

	for (position, out) in placement
		.positions
		.iter()
		.zip(out.as_chunks_mut::<3>().0.iter_mut())
	{
		out.copy_from_slice(&position.to_array());
	}
}

/// Writes, for each point of the grid, the index of its probe among the volume's, or
/// `RX_PLACEMENT_NO_PROBE`.
///
/// # Safety
///
/// `placement` must be live and `out` must have room for a value per grid point.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_placement_grid(placement: *const RxPlacement, out: *mut u32)
{
	// SAFETY: guaranteed by the caller.
	let placement = unsafe { &*placement };

	// SAFETY: guaranteed by the caller.
	let out = unsafe { slice::from_raw_parts_mut(out, placement.grid.len()) };

	for (probe, out) in placement.grid.iter().zip(out) {
		*out = probe.unwrap_or(RX_PLACEMENT_NO_PROBE);
	}
}

/// # Safety
///
/// `region_size` must hold three floats and `out_dims` room for three values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_grid_for_spacing(
	region_size: *const f32,
	spacing: f32,
	cell_centred: bool,
	out_dims: *mut u32,
)
{
	let grid = probe_placement::grid_for_spacing(vec3(region_size), spacing, cell_centred);

	// SAFETY: guaranteed by the caller.
	unsafe { slice::from_raw_parts_mut(out_dims, 3) }.copy_from_slice(&grid);
}

/// # Safety
///
/// The vectors and dims must hold three values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_layout_grid(
	region_min: *const f32,
	region_max: *const f32,
	grid_dims: *const u32,
	cell_centred: bool,
	out_min: *mut f32,
	out_size: *mut f32,
)
{
	let (min, size) = probe_placement::layout_grid(
		vec3(region_min),
		vec3(region_max),
		dims(grid_dims),
		cell_centred,
	);

	write_vec3(out_min, min);
	write_vec3(out_size, size);
}

/// # Safety
///
/// The vectors and dims must hold three values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_grid_cell_size(
	volume_size: *const f32,
	grid_dims: *const u32,
	out_size: *mut f32,
)
{
	write_vec3(
		out_size,
		probe_placement::grid_cell_size(vec3(volume_size), dims(grid_dims)),
	);
}

/// # Safety
///
/// `boxes` must be live and the positions must hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_position_valid(
	boxes: *const RxPlacementBoxes,
	grid_position: *const f32,
	position: *const f32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	let boxes = unsafe { &*boxes };

	probe_placement::is_probe_placement_valid(vec3(grid_position), vec3(position), boxes)
}

/// Looks around a grid point for the valid position nearest to `preferred`, and writes it to
/// `out_position`.
///
/// # Safety
///
/// `boxes` must be live and the vectors must hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_find_valid_position(
	boxes: *const RxPlacementBoxes,
	grid_position: *const f32,
	preferred: *const f32,
	max_relocation: *const f32,
	out_position: *mut f32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	let boxes = unsafe { &*boxes };

	match probe_placement::find_valid_probe_position(
		vec3(grid_position),
		vec3(preferred),
		vec3(max_relocation),
		boxes,
	) {
		Some(position) => {
			write_vec3(out_position, position);
			true
		}
		None => false,
	}
}
