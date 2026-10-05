use raptor_math::{Aabb, Mat4f, Vec3f, Vec4f};

pub const SURFACE_OFFSET: f32 = 0.25;
pub const HUG_SURFACE_OFFSET: f32 = 1.0;

/// Probes this close to the outside of a box still count as inside it
pub const INSIDE_SKIN: f32 = 0.05;

/// Probes in open space further than this from a surface are left where they are
pub const HUG_MAX_DISTANCE: f32 = 1.5;
pub const MAX_PUSH_OUT_ITERATIONS: u32 = 8;

/// Furthest a probe can move from its grid point, as a fraction of a cell
pub const MAX_RELOCATION: f32 = 0.45;

pub const CROSSING_SKIN: f32 = 0.01;
pub const CAPTURE_NEAR_PLANE: f32 = 0.1;
pub const MIN_CLEARANCE: f32 = 2.0 * CAPTURE_NEAR_PLANE;
pub const RELOCATION_SEARCH_STEPS: u32 = 5;

pub const SURFACE_CELL_MARGIN: f32 = 0.05;

pub const SHADER_NORMAL_BIAS_SCALE: f32 = 0.2;
pub const SHADER_NORMAL_BIAS_MIN: f32 = 0.02;
pub const SHADER_NORMAL_BIAS_MAX: f32 = 0.5;

pub const MIN_GRID_DIM: u32 = 2;
pub const MAX_BOXES: usize = 256;
pub const MAX_PLANES: usize = 32;

#[derive(Clone, Copy, Debug)]
pub struct Plane
{
	pub normal: Vec3f,
	pub distance: f32,
}

/// What a probe has to stay out of: the solid of a piece of level geometry.
pub struct PlacementBox
{
	pub local_bounds: Aabb,
	pub world_bounds: Aabb,
	pub axis_aligned: bool,
	pub local_to_world: Mat4f,
	pub world_to_local: Mat4f,
	/// The bounding planes of a brush that is not a box, in local space. Without them the solid is
	/// the local bounds, which for a slanted brush would also take in the empty space beside
	/// the slope.
	pub planes: Vec<Plane>,
}

pub struct PlacementBoxes
{
	boxes: Vec<PlacementBox>,
	min: Vec3f,
	max: Vec3f,
}

impl Default for PlacementBoxes
{
	fn default() -> Self
	{
		Self {
			boxes: Vec::new(),
			min: Vec3f::splat(f32::MAX),
			max: Vec3f::splat(-f32::MAX),
		}
	}
}

impl PlacementBoxes
{
	/// Adds a box, unless there is no room for more. Returns whether it was added.
	pub fn add(&mut self, placement_box: PlacementBox) -> bool
	{
		if self.boxes.len() >= MAX_BOXES {
			return false;
		}

		self.boxes.push(placement_box);

		true
	}

	/// Grows the bounds of the level to hold a box, which can be one there was no room for.
	pub fn extend_bounds(&mut self, min: Vec3f, max: Vec3f)
	{
		self.min = self.min.min(&min);
		self.max = self.max.max(&max);
	}

	pub fn len(&self) -> usize
	{
		self.boxes.len()
	}

	pub fn is_empty(&self) -> bool
	{
		self.min.x > self.max.x
	}

	pub fn min(&self) -> Vec3f
	{
		self.min
	}

	pub fn max(&self) -> Vec3f
	{
		self.max
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Fill
{
	Dense,
	Surface,
}

fn axis(v: Vec3f, index: usize) -> f32
{
	[v.x, v.y, v.z][index]
}

fn with_axis(v: Vec3f, index: usize, value: f32) -> Vec3f
{
	let mut values = v.to_array();

	values[index] = value;

	Vec3f::from_array(values)
}

fn to_box_local(point: Vec3f, placement_box: &PlacementBox) -> Vec3f
{
	let local = placement_box.world_to_local * Vec4f::new(point.x, point.y, point.z, 1.0);

	Vec3f::new(local.x, local.y, local.z)
}

fn to_world(local: Vec3f, placement_box: &PlacementBox) -> Vec3f
{
	let world = placement_box.local_to_world * Vec4f::new(local.x, local.y, local.z, 1.0);

	Vec3f::new(world.x, world.y, world.z)
}

fn is_inside_box(point: Vec3f, placement_box: &PlacementBox, skin: f32) -> bool
{
	let local = to_box_local(point, placement_box);

	if !placement_box.planes.is_empty() {
		for plane in &placement_box.planes {
			if plane.normal.dot(&local) - plane.distance >= skin {
				return false;
			}
		}

		return true;
	}

	for index in 0..3 {
		if axis(local, index) <= axis(placement_box.local_bounds.min, index) - skin
			|| axis(local, index) >= axis(placement_box.local_bounds.max, index) + skin
		{
			return false;
		}
	}

	true
}

fn is_inside_any_box(point: Vec3f, boxes: &PlacementBoxes) -> bool
{
	boxes
		.boxes
		.iter()
		.any(|placement_box| is_inside_box(point, placement_box, INSIDE_SKIN))
}

fn is_inside_level(point: Vec3f, boxes: &PlacementBoxes) -> bool
{
	for index in 0..3 {
		if axis(point, index) < axis(boxes.min, index)
			|| axis(point, index) > axis(boxes.max, index)
		{
			return false;
		}
	}

	true
}

/// The point of the box's solid nearest to `local`, in the box's local space
fn closest_point_in_box(local: Vec3f, placement_box: &PlacementBox) -> Vec3f
{
	if placement_box.planes.is_empty() {
		return local.clamp(
			&placement_box.local_bounds.min,
			&placement_box.local_bounds.max,
		);
	}

	// Projecting onto the planes one after another gives a point inside the hull, but not
	// necessarily the nearest one. Dykstra's correction terms make it converge on the nearest,
	// and points that are already inside stop at once.
	const MAX_ITERATIONS: u32 = 32;
	const CONVERGED_SQUARED: f32 = 1e-8;

	let mut corrections = [Vec3f::ZERO; MAX_PLANES];

	let plane_count = placement_box.planes.len().min(MAX_PLANES);

	let mut closest = local;

	for _ in 0..MAX_ITERATIONS {
		let mut moved_squared = 0.0f32;

		for (plane, correction) in placement_box.planes[..plane_count]
			.iter()
			.zip(corrections.iter_mut())
		{
			let shifted = closest + *correction;
			let outside = plane.normal.dot(&shifted) - plane.distance;
			let projected = if outside > 0.0 {
				shifted - plane.normal * outside
			} else {
				shifted
			};

			*correction = shifted - projected;

			let step = projected - closest;
			moved_squared = moved_squared.max(step.dot(&step));
			closest = projected;
		}

		if moved_squared < CONVERGED_SQUARED {
			break;
		}
	}

	closest
}

fn distance_to_box(point: Vec3f, placement_box: &PlacementBox) -> f32
{
	let closest_local = closest_point_in_box(to_box_local(point, placement_box), placement_box);

	(point - to_world(closest_local, placement_box)).length()
}

fn segment_crosses_box(from: Vec3f, to: Vec3f, placement_box: &PlacementBox, skin: f32) -> bool
{
	let local_from = to_box_local(from, placement_box);
	let local_to = to_box_local(to, placement_box);

	let mut t_enter = 0.0f32;
	let mut t_exit = 1.0f32;

	if !placement_box.planes.is_empty() {
		// Clip the segment against each plane pulled in by the skin
		for plane in &placement_box.planes {
			let limit = plane.distance - skin;
			let start = plane.normal.dot(&local_from);
			let delta = plane.normal.dot(&local_to) - start;

			if delta.abs() < 1e-6 {
				if start >= limit {
					return false;
				}

				continue;
			}

			let t = (limit - start) / delta;

			if delta < 0.0 {
				t_enter = t_enter.max(t);
			} else {
				t_exit = t_exit.min(t);
			}

			if t_enter >= t_exit {
				return false;
			}
		}

		return true;
	}

	for index in 0..3 {
		let lo = axis(placement_box.local_bounds.min, index) + skin;
		let hi = axis(placement_box.local_bounds.max, index) - skin;
		let start = axis(local_from, index);
		let delta = axis(local_to, index) - start;

		if delta.abs() < 1e-6 {
			if start <= lo || start >= hi {
				return false;
			}

			continue;
		}

		let mut t0 = (lo - start) / delta;
		let mut t1 = (hi - start) / delta;

		if t0 > t1 {
			std::mem::swap(&mut t0, &mut t1);
		}

		t_enter = t_enter.max(t0);
		t_exit = t_exit.min(t1);

		if t_enter >= t_exit {
			return false;
		}
	}

	true
}

/// Whether a probe can sit at `position`, having started at `grid_position`.
pub fn is_probe_placement_valid(
	grid_position: Vec3f,
	position: Vec3f,
	boxes: &PlacementBoxes,
) -> bool
{
	if boxes.is_empty() {
		return true;
	}

	if !is_inside_level(position, boxes) {
		return false;
	}

	for placement_box in &boxes.boxes {
		if distance_to_box(position, placement_box) < MIN_CLEARANCE {
			return false;
		}

		// Leaving the box the grid point started in is what the push out is for
		if !is_inside_box(grid_position, placement_box, INSIDE_SKIN)
			&& segment_crosses_box(grid_position, position, placement_box, CROSSING_SKIN)
		{
			return false;
		}
	}

	true
}

/// Boxes have six faces, and a brush has one per plane
fn face_count(placement_box: &PlacementBox) -> u32
{
	if placement_box.planes.is_empty() {
		6
	} else {
		placement_box.planes.len() as u32
	}
}

/// How far `local` has to move to leave through a face. Box faces are numbered axis * 2 + (0 for
/// min, 1 for max).
fn face_penetration(local: Vec3f, placement_box: &PlacementBox, face: u32) -> f32
{
	if !placement_box.planes.is_empty() {
		let plane = &placement_box.planes[face as usize];

		return plane.distance - plane.normal.dot(&local);
	}

	let index = (face / 2) as usize;

	if face & 1 != 0 {
		axis(placement_box.local_bounds.max, index) - axis(local, index)
	} else {
		axis(local, index) - axis(placement_box.local_bounds.min, index)
	}
}

/// Where `local` comes out of a face, and the way the face points, both in world space.
fn face_exit_point(local: Vec3f, placement_box: &PlacementBox, face: u32) -> (Vec3f, Vec3f)
{
	let mut exit_local = local;
	let mut local_normal = Vec3f::ZERO;

	if !placement_box.planes.is_empty() {
		let plane = &placement_box.planes[face as usize];

		exit_local = local + plane.normal * (plane.distance - plane.normal.dot(&local));
		local_normal = plane.normal;
	} else {
		let index = (face / 2) as usize;
		let through_max = face & 1 != 0;

		exit_local = with_axis(
			exit_local,
			index,
			if through_max {
				axis(placement_box.local_bounds.max, index)
			} else {
				axis(placement_box.local_bounds.min, index)
			},
		);
		local_normal = with_axis(local_normal, index, if through_max { 1.0 } else { -1.0 });
	}

	let exit_world = to_world(exit_local, placement_box);

	let world_normal = placement_box.local_to_world
		* Vec4f::new(local_normal.x, local_normal.y, local_normal.z, 0.0);

	(
		exit_world,
		Vec3f::new(world_normal.x, world_normal.y, world_normal.z).normalize(),
	)
}

fn exit_through_face(local: Vec3f, placement_box: &PlacementBox, face: u32) -> Vec3f
{
	let (exit, world_normal) = face_exit_point(local, placement_box, face);

	exit + world_normal * SURFACE_OFFSET
}

fn distance_to_box_surface(point: Vec3f, placement_box: &PlacementBox) -> f32
{
	if !is_inside_box(point, placement_box, 0.0) {
		return distance_to_box(point, placement_box);
	}

	let local = to_box_local(point, placement_box);

	let mut nearest = f32::MAX;

	for face in 0..face_count(placement_box) {
		let (exit, _) = face_exit_point(local, placement_box, face);

		nearest = nearest.min((exit - point).length());
	}

	nearest
}

fn cell_touches_surface(cell_min: Vec3f, cell_max: Vec3f, pad: f32, boxes: &PlacementBoxes)
-> bool
{
	let padded_min = cell_min - Vec3f::splat(pad);
	let padded_max = cell_max + Vec3f::splat(pad);

	let center = (cell_min + cell_max) * 0.5;
	let radius = ((cell_max - cell_min) * 0.5).length() + pad;

	for placement_box in &boxes.boxes {
		let bounds = &placement_box.world_bounds;

		let mut overlaps = true;
		let mut contained = true;

		for index in 0..3 {
			overlaps &= axis(padded_min, index) < axis(bounds.max, index)
				&& axis(padded_max, index) > axis(bounds.min, index);
			contained &= axis(padded_min, index) >= axis(bounds.min, index)
				&& axis(padded_max, index) <= axis(bounds.max, index);
		}

		if !overlaps {
			continue;
		}

		if placement_box.axis_aligned {
			if !contained {
				return true;
			}

			continue;
		}

		if distance_to_box_surface(center, placement_box) <= radius {
			return true;
		}
	}

	false
}

pub fn shader_normal_bias(cell_size: Vec3f) -> f32
{
	let min_spacing = cell_size.x.min(cell_size.y).min(cell_size.z);

	(SHADER_NORMAL_BIAS_SCALE * min_spacing).clamp(SHADER_NORMAL_BIAS_MIN, SHADER_NORMAL_BIAS_MAX)
}

fn is_buried_too_deep(point: Vec3f, max_relocation: Vec3f, boxes: &PlacementBoxes) -> bool
{
	for placement_box in &boxes.boxes {
		if !is_inside_box(point, placement_box, 0.0) {
			continue;
		}

		if !placement_box.axis_aligned {
			if distance_to_box_surface(point, placement_box) + MIN_CLEARANCE
				> max_relocation.length()
			{
				return true;
			}

			continue;
		}

		let mut can_escape = false;

		for index in 0..3 {
			let to_min = axis(point, index) - axis(placement_box.world_bounds.min, index);
			let to_max = axis(placement_box.world_bounds.max, index) - axis(point, index);

			can_escape |= to_min.min(to_max) + MIN_CLEARANCE <= axis(max_relocation, index);
		}

		if !can_escape {
			return true;
		}
	}

	false
}

/// Which points of a grid get a probe, and how many.
pub fn find_needed_grid_points(
	volume_min: Vec3f,
	cell_size: Vec3f,
	dims: [u32; 3],
	boxes: &PlacementBoxes,
	fill: Fill,
) -> (u32, Vec<u8>)
{
	let num_points = (dims[0] * dims[1] * dims[2]) as usize;

	let all_needed = fill == Fill::Dense || boxes.boxes.is_empty();

	let mut needed = vec![u8::from(all_needed); num_points];

	if boxes.boxes.is_empty() {
		return (num_points as u32, needed);
	}

	let pad = shader_normal_bias(cell_size) + SURFACE_CELL_MARGIN;

	if fill == Fill::Surface {
		for iz in 0..dims[2].saturating_sub(1) {
			for iy in 0..dims[1].saturating_sub(1) {
				for ix in 0..dims[0].saturating_sub(1) {
					let cell_min =
						volume_min + cell_size * Vec3f::new(ix as f32, iy as f32, iz as f32);

					if !cell_touches_surface(cell_min, cell_min + cell_size, pad, boxes) {
						continue;
					}

					for corner in 0..8u32 {
						let cx = ix + (corner & 1);
						let cy = iy + ((corner >> 1) & 1);
						let cz = iz + ((corner >> 2) & 1);

						needed[(cx + dims[0] * (cy + dims[1] * cz)) as usize] = 1;
					}
				}
			}
		}
	}

	let max_relocation = cell_size * MAX_RELOCATION;

	let mut count = 0;
	let mut point = 0usize;

	for iz in 0..dims[2] {
		for iy in 0..dims[1] {
			for ix in 0..dims[0] {
				let index = point;
				point += 1;

				if needed[index] == 0 {
					continue;
				}

				let position = volume_min + cell_size * Vec3f::new(ix as f32, iy as f32, iz as f32);

				if is_buried_too_deep(position, max_relocation, boxes) {
					needed[index] = 0;
					continue;
				}

				count += 1;
			}
		}
	}

	(count, needed)
}

fn push_out_of_box(point: &mut Vec3f, placement_box: &PlacementBox) -> bool
{
	if !is_inside_box(*point, placement_box, INSIDE_SKIN) {
		return false;
	}

	let local = to_box_local(*point, placement_box);

	let mut exit_face = 0;
	let mut exit_distance = f32::MAX;

	for face in 0..face_count(placement_box) {
		let penetration = face_penetration(local, placement_box, face);

		if penetration < exit_distance {
			exit_distance = penetration;
			exit_face = face;
		}
	}

	*point = exit_through_face(local, placement_box, exit_face);

	true
}

/// Moves a point that is inside of geometry out of it, to the nearest place that is a valid
/// position if there is one. Returns whether it was inside.
pub fn push_out_of_boxes(point: &mut Vec3f, boxes: &PlacementBoxes) -> bool
{
	const NO_EXIT: u32 = 3;

	let mut best_rank = NO_EXIT;
	let mut best_distance = f32::MAX;
	let mut best_exit = *point;
	let mut inside = false;

	for placement_box in &boxes.boxes {
		if !is_inside_box(*point, placement_box, INSIDE_SKIN) {
			continue;
		}

		inside = true;

		let local = to_box_local(*point, placement_box);

		for face in 0..face_count(placement_box) {
			let exit = exit_through_face(local, placement_box, face);
			let distance = (exit - *point).length();

			let mut rank = NO_EXIT;

			if is_probe_placement_valid(*point, exit, boxes) {
				rank = 0;
			} else if !is_inside_any_box(exit, boxes) {
				rank = if is_inside_level(exit, boxes) { 1 } else { 2 };
			}

			if rank < best_rank
				|| (rank == best_rank && rank != NO_EXIT && distance < best_distance)
			{
				best_rank = rank;
				best_distance = distance;
				best_exit = exit;
			}
		}
	}

	if !inside {
		return false;
	}

	if best_rank != NO_EXIT {
		*point = best_exit;
		return true;
	}

	// Every face leads into more geometry, so step out one box at a time and leave the result to
	// the placement check
	for _ in 0..MAX_PUSH_OUT_ITERATIONS {
		let mut pushed = false;

		for placement_box in &boxes.boxes {
			pushed = push_out_of_box(point, placement_box);

			if pushed {
				break;
			}
		}

		if !pushed {
			break;
		}
	}

	true
}

/// Distance from `point` to the nearest box other than `skip_box`
fn distance_to_other_boxes(point: Vec3f, boxes: &PlacementBoxes, skip_box: usize) -> f32
{
	let mut nearest = f32::MAX;

	for (index, placement_box) in boxes.boxes.iter().enumerate() {
		if index != skip_box {
			nearest = nearest.min(distance_to_box(point, placement_box));
		}
	}

	nearest
}

/// Moves a point in open space towards the nearest surface, to sit just off it. Returns whether it
/// moved.
pub fn hug_nearest_surface(point: &mut Vec3f, boxes: &PlacementBoxes, max_distance: f32) -> bool
{
	let mut best_distance = max_distance;
	let mut best_box = 0;
	let mut best_closest = *point;
	let mut found = false;

	for (index, placement_box) in boxes.boxes.iter().enumerate() {
		let closest_local =
			closest_point_in_box(to_box_local(*point, placement_box), placement_box);
		let closest = to_world(closest_local, placement_box);

		let distance = (*point - closest).length();

		// Points on or inside a box are left to the push out
		if distance < 1e-6 || distance >= best_distance {
			continue;
		}

		best_distance = distance;
		best_box = index;
		best_closest = closest;
		found = true;
	}

	if !found {
		return false;
	}

	let direction = (*point - best_closest) * (1.0 / best_distance);

	let mut target = HUG_SURFACE_OFFSET;

	let keeps_clear = |along: f32| {
		distance_to_other_boxes(best_closest + direction * along, boxes, best_box) >= along
	};

	if target > best_distance && !keeps_clear(target) {
		let mut clear = best_distance;
		let mut blocked = target;

		for _ in 0..16 {
			let middle = (clear + blocked) * 0.5;

			if keeps_clear(middle) {
				clear = middle;
			} else {
				blocked = middle;
			}
		}

		target = clear;
	}

	*point = best_closest + direction * target;

	true
}

/// Looks around a grid point for the valid position nearest to `preferred`.
pub fn find_valid_probe_position(
	grid_position: Vec3f,
	preferred: Vec3f,
	max_relocation: Vec3f,
	boxes: &PlacementBoxes,
) -> Option<Vec3f>
{
	const STEP_SCALE: f32 = 2.0 / (RELOCATION_SEARCH_STEPS - 1) as f32;

	let mut best_distance = f32::MAX;
	let mut found = None;

	for iz in 0..RELOCATION_SEARCH_STEPS {
		for iy in 0..RELOCATION_SEARCH_STEPS {
			for ix in 0..RELOCATION_SEARCH_STEPS {
				let step =
					Vec3f::new(ix as f32, iy as f32, iz as f32) * STEP_SCALE - Vec3f::splat(1.0);

				let candidate = grid_position + max_relocation * step;
				let distance = (candidate - preferred).length();

				if distance >= best_distance
					|| !is_probe_placement_valid(grid_position, candidate, boxes)
				{
					continue;
				}

				best_distance = distance;
				found = Some(candidate);
			}
		}
	}

	found
}

/// How many probes fit along an extent at about `spacing`
pub fn slices_for_extent(extent: f32, spacing: f32) -> u32
{
	let slices = extent.max(0.0) / spacing.max(1e-3);

	(slices.round() as u32).max(MIN_GRID_DIM)
}

/// The grid that covers a region at about `spacing`. A cell centred grid has its probes in the
/// middle of cells rather than on their corners, so it has one fewer along each axis.
pub fn grid_for_spacing(region_size: Vec3f, spacing: f32, cell_centred: bool) -> [u32; 3]
{
	let extra = u32::from(!cell_centred);

	[
		slices_for_extent(region_size.x, spacing) + extra,
		slices_for_extent(region_size.y, spacing) + extra,
		slices_for_extent(region_size.z, spacing) + extra,
	]
}

/// The volume that puts the probes of a cell centred grid in the middle of the cells of a region.
pub fn fit_cell_centred(region_min: Vec3f, region_max: Vec3f, dims: [u32; 3]) -> (Vec3f, Vec3f)
{
	let region_size = region_max - region_min;
	let slices = Vec3f::new(dims[0] as f32, dims[1] as f32, dims[2] as f32);
	let inset = region_size / (slices * 2.0);

	(region_min + inset, region_size - inset * 2.0)
}

pub fn layout_grid(
	region_min: Vec3f,
	region_max: Vec3f,
	dims: [u32; 3],
	cell_centred: bool,
) -> (Vec3f, Vec3f)
{
	if cell_centred {
		return fit_cell_centred(region_min, region_max, dims);
	}

	(region_min, region_max - region_min)
}

pub fn grid_cell_size(volume_size: Vec3f, dims: [u32; 3]) -> Vec3f
{
	let num_cells = Vec3f::new(
		(dims[0] - 1) as f32,
		(dims[1] - 1) as f32,
		(dims[2] - 1) as f32,
	);

	volume_size / num_cells
}

#[derive(Debug, PartialEq, Eq)]
pub struct TooManyProbes;

/// Where the probes of a volume go.
pub struct Placement
{
	/// For each point of the grid, the index among the volume's probes of the one placed there
	pub grid: Vec<Option<u32>>,
	pub positions: Vec<Vec3f>,
	pub needed: u32,
	pub pushed: u32,
	pub hugged: u32,
	pub relocated: u32,
	pub unplaced: u32,
}

/// Places a probe at each point of a grid that needs one, moving it out of geometry, then to sit
/// just off a nearby surface, and failing that to somewhere near that is valid. Fails if more than
/// `probe_budget` are placed.
pub fn place_volume(
	boxes: &PlacementBoxes,
	volume_min: Vec3f,
	volume_size: Vec3f,
	dims: [u32; 3],
	fill: Fill,
	probe_budget: u32,
) -> Result<Placement, TooManyProbes>
{
	// Probes sit on the volume's boundary, so there are (dim - 1) cells along each axis
	let cell_size = grid_cell_size(volume_size, dims);

	let hug_distance = HUG_MAX_DISTANCE
		.min(0.5 * cell_size.x)
		.min(0.5 * cell_size.y)
		.min(0.5 * cell_size.z);

	let max_relocation = cell_size * MAX_RELOCATION;

	let (needed_count, needed) = find_needed_grid_points(volume_min, cell_size, dims, boxes, fill);

	let mut placement = Placement {
		grid: Vec::with_capacity(needed.len()),
		positions: Vec::new(),
		needed: needed_count,
		pushed: 0,
		hugged: 0,
		relocated: 0,
		unplaced: 0,
	};

	let mut point = 0usize;

	for iz in 0..dims[2] {
		for iy in 0..dims[1] {
			for ix in 0..dims[0] {
				let index = point;
				point += 1;

				placement.grid.push(None);

				if needed[index] == 0 {
					continue;
				}

				let grid_position =
					volume_min + cell_size * Vec3f::new(ix as f32, iy as f32, iz as f32);
				let mut position = grid_position;

				if push_out_of_boxes(&mut position, boxes) {
					placement.pushed += 1;
				}

				if hug_nearest_surface(&mut position, boxes, hug_distance) {
					// The surface of one box can be inside another
					push_out_of_boxes(&mut position, boxes);
					placement.hugged += 1;
				}

				position = position.clamp(
					&(grid_position - max_relocation),
					&(grid_position + max_relocation),
				);

				if !is_probe_placement_valid(grid_position, position, boxes) {
					let Some(fallback) =
						find_valid_probe_position(grid_position, position, max_relocation, boxes)
					else {
						placement.unplaced += 1;
						continue;
					};

					position = fallback;
					placement.relocated += 1;
				}

				if placement.positions.len() as u32 >= probe_budget {
					return Err(TooManyProbes);
				}

				placement.grid[index] = Some(placement.positions.len() as u32);
				placement.positions.push(position);
			}
		}
	}

	Ok(placement)
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn axis_aligned_box(min: [f32; 3], max: [f32; 3]) -> PlacementBox
	{
		let bounds = Aabb::new(Vec3f::from_array(min), Vec3f::from_array(max));

		PlacementBox {
			local_bounds: bounds,
			world_bounds: bounds,
			axis_aligned: true,
			local_to_world: Mat4f::identity(),
			world_to_local: Mat4f::identity(),
			planes: Vec::new(),
		}
	}

	fn level(boxes: Vec<PlacementBox>) -> PlacementBoxes
	{
		let mut result = PlacementBoxes::default();

		for placement_box in boxes {
			result.extend_bounds(
				placement_box.world_bounds.min,
				placement_box.world_bounds.max,
			);
			result.add(placement_box);
		}

		result
	}

	#[test]
	fn an_empty_level_has_no_geometry_and_accepts_any_position()
	{
		let boxes = PlacementBoxes::default();

		assert!(boxes.is_empty());
		assert!(is_probe_placement_valid(
			Vec3f::ZERO,
			Vec3f::splat(100.0),
			&boxes
		));
	}

	#[test]
	fn a_point_inside_a_wall_is_pushed_out_through_the_nearest_face()
	{
		let boxes = level(vec![axis_aligned_box(
			[-10.0, 0.0, -10.0],
			[10.0, 1.0, 10.0],
		)]);

		let mut point = Vec3f::new(0.0, 0.8, 0.0);

		assert!(push_out_of_boxes(&mut point, &boxes));

		assert!(point.y > 1.0);
		assert!(point.y < 1.0 + SURFACE_OFFSET + 1e-4);
		assert_eq!(point.x, 0.0);
	}

	#[test]
	fn a_point_in_open_space_is_left_by_the_push_out()
	{
		let boxes = level(vec![axis_aligned_box([-1.0, -1.0, -1.0], [1.0, 1.0, 1.0])]);

		let mut point = Vec3f::new(0.0, 5.0, 0.0);

		assert!(!push_out_of_boxes(&mut point, &boxes));
		assert_eq!(point, Vec3f::new(0.0, 5.0, 0.0));
	}

	#[test]
	fn hugging_moves_a_nearby_point_to_just_off_the_surface()
	{
		let boxes = level(vec![axis_aligned_box([-5.0, -1.0, -5.0], [5.0, 0.0, 5.0])]);

		let mut point = Vec3f::new(0.0, 0.8, 0.0);

		assert!(hug_nearest_surface(&mut point, &boxes, 1.5));
		assert!((point.y - HUG_SURFACE_OFFSET).abs() < 1e-5);

		let mut far = Vec3f::new(0.0, 4.0, 0.0);

		assert!(!hug_nearest_surface(&mut far, &boxes, 1.5));
	}

	#[test]
	fn a_segment_through_a_box_is_a_crossing_and_one_beside_it_is_not()
	{
		let boxes = level(vec![axis_aligned_box([-1.0, -1.0, -1.0], [1.0, 1.0, 1.0])]);
		let placement_box = &boxes.boxes[0];

		assert!(segment_crosses_box(
			Vec3f::new(-3.0, 0.0, 0.0),
			Vec3f::new(3.0, 0.0, 0.0),
			placement_box,
			CROSSING_SKIN
		));
		assert!(!segment_crosses_box(
			Vec3f::new(-3.0, 3.0, 0.0),
			Vec3f::new(3.0, 3.0, 0.0),
			placement_box,
			CROSSING_SKIN
		));
	}

	#[test]
	fn slices_round_to_the_nearest_and_never_go_below_the_minimum()
	{
		assert_eq!(slices_for_extent(10.0, 2.5), 4);
		assert_eq!(slices_for_extent(10.0, 2.4), 4);
		assert_eq!(slices_for_extent(0.1, 5.0), MIN_GRID_DIM);
		assert_eq!(slices_for_extent(-3.0, 1.0), MIN_GRID_DIM);
	}

	#[test]
	fn a_cell_centred_grid_has_one_fewer_probe_along_each_axis()
	{
		let size = Vec3f::new(10.0, 10.0, 10.0);

		assert_eq!(grid_for_spacing(size, 2.5, false), [5, 5, 5]);
		assert_eq!(grid_for_spacing(size, 2.5, true), [4, 4, 4]);

		let (min, volume) = fit_cell_centred(Vec3f::ZERO, size, [4, 4, 4]);

		assert_eq!(min, Vec3f::splat(1.25));
		assert_eq!(volume, Vec3f::splat(7.5));
	}

	#[test]
	fn a_dense_volume_in_an_empty_level_gets_a_probe_at_every_point()
	{
		let boxes = PlacementBoxes::default();

		let placement = place_volume(
			&boxes,
			Vec3f::ZERO,
			Vec3f::new(4.0, 2.0, 4.0),
			[3, 2, 3],
			Fill::Dense,
			100,
		)
		.unwrap();

		assert_eq!(placement.positions.len(), 18);
		assert_eq!(placement.needed, 18);
		assert!(placement.grid.iter().all(Option::is_some));
		assert_eq!(placement.positions[0], Vec3f::ZERO);
		assert_eq!(placement.positions[1], Vec3f::new(2.0, 0.0, 0.0));
	}

	#[test]
	fn placing_more_probes_than_the_budget_fails()
	{
		let boxes = PlacementBoxes::default();

		let placement = place_volume(
			&boxes,
			Vec3f::ZERO,
			Vec3f::new(4.0, 2.0, 4.0),
			[3, 2, 3],
			Fill::Dense,
			5,
		);

		assert!(matches!(placement, Err(TooManyProbes)));
	}

	#[test]
	fn probes_are_kept_out_of_geometry()
	{
		let mut boxes = level(vec![axis_aligned_box(
			[-20.0, -0.5, -20.0],
			[20.0, 0.5, 20.0],
		)]);

		boxes.extend_bounds(Vec3f::new(-20.0, -5.0, -20.0), Vec3f::new(20.0, 5.0, 20.0));

		let placement = place_volume(
			&boxes,
			Vec3f::new(-4.0, -2.0, -4.0),
			Vec3f::new(8.0, 4.0, 8.0),
			[5, 5, 5],
			Fill::Dense,
			1000,
		)
		.unwrap();

		assert!(!placement.positions.is_empty());

		for position in &placement.positions {
			assert!(!is_inside_any_box(*position, &boxes), "{position:?}");
		}
	}
}
