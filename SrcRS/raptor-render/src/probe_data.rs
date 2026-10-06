use std::mem::size_of;

use raptor_math::{Mat4f, Vec3f};

use crate::probe_placement::{
	Fill, PlacementBoxes, find_needed_grid_points, grid_cell_size, grid_for_spacing, layout_grid,
};

pub const MAX_VOLUMES: u32 = 8;
pub const MAX_PROBES: u32 = 4096;
pub const MAX_GRID_POINTS: u32 = 1 << 20;
pub const GRID_EMPTY: u16 = 0xFFFF;
pub const SH_COEFF_COUNT: u32 = 9;
pub const DEPTH_SIZE: u32 = 16;
pub const DEPTH_FACES: u32 = 6;
pub const DEPTH_TEXELS_PER_FACE: u32 = DEPTH_SIZE * DEPTH_SIZE;
pub const DEPTH_FLOAT_COUNT: u32 = DEPTH_FACES * DEPTH_TEXELS_PER_FACE * 2;
pub const DEPTH_MAX_DISTANCE: f32 = 50.0;
pub const ATLAS_COLUMNS: u32 = 32;
pub const ATLAS_PROBE_WIDTH: u32 = DEPTH_FACES * DEPTH_SIZE;
pub const ATLAS_WIDTH: u32 = ATLAS_COLUMNS * ATLAS_PROBE_WIDTH;
pub const MAX_REFLECTION_PROBES: u32 = 16;
pub const REFLECTION_SIZE: u32 = 128;
pub const REFLECTION_MIPS: u32 = 6;

pub const CACHE_FILE_VERSION: u32 = 12;
pub const REFLECTION_FILE_VERSION: u32 = 1;

const SH_BYTES: u64 = (SH_COEFF_COUNT * 16) as u64;
const INFO_BYTES: u64 = (DEPTH_FLOAT_COUNT as u64 + 4) * 4;
const REFLECTION_PROBE_BYTES: u64 = 96;

pub const REFLECTION_HALFS_PER_PROBE: u64 =
	6 * (128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4) * 4;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct VolumeData
{
	pub min_and_count: [f32; 4],
	pub inv_cell_size: [f32; 4],
	pub dims_and_first: [u32; 4],
	pub max_and_cell_volume: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VolumeRange
{
	pub first_probe: u32,
	pub probe_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FileLayout
{
	pub magic: [u8; 4],
	pub version: u32,
	pub sh_coeff_count: u32,
	pub depth_float_count: u32,
	pub max_volumes: u32,
}

impl Default for FileLayout
{
	fn default() -> Self
	{
		Self {
			magic: *b"RPPV",
			version: CACHE_FILE_VERSION,
			sh_coeff_count: SH_COEFF_COUNT,
			depth_float_count: DEPTH_FLOAT_COUNT,
			max_volumes: MAX_VOLUMES,
		}
	}
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FileHeader
{
	pub layout: FileLayout,
	pub volume_count: u32,
	pub probe_count: u32,
	pub grid_point_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReflectionFileHeader
{
	pub magic: [u8; 4],
	pub version: u32,
	pub size: u32,
	pub mips: u32,
	pub probe_count: u32,
}

impl Default for ReflectionFileHeader
{
	fn default() -> Self
	{
		Self {
			magic: *b"RPRF",
			version: REFLECTION_FILE_VERSION,
			size: REFLECTION_SIZE,
			mips: REFLECTION_MIPS,
			probe_count: 0,
		}
	}
}

const _: () = {
	assert!(size_of::<VolumeData>() == 64);
	assert!(size_of::<VolumeRange>() == 8);
	assert!(size_of::<FileHeader>() == 32);
	assert!(size_of::<ReflectionFileHeader>() == 20);
};

pub fn probe_file_size(probe_count: u32, grid_point_count: u32) -> u64
{
	size_of::<FileHeader>() as u64
		+ u64::from(MAX_VOLUMES) * (size_of::<VolumeData>() + size_of::<VolumeRange>()) as u64
		+ u64::from(grid_point_count) * 2
		+ u64::from(probe_count) * (SH_BYTES + INFO_BYTES)
}

pub fn layout_readable(layout: &FileLayout) -> bool
{
	let expected = FileLayout::default();

	layout.magic == expected.magic
		&& layout.sh_coeff_count == expected.sh_coeff_count
		&& layout.depth_float_count == expected.depth_float_count
		&& layout.max_volumes == expected.max_volumes
		&& layout.version == CACHE_FILE_VERSION
}

#[derive(Debug, PartialEq, Eq)]
pub enum FileError
{
	OutOfDate,
	PastLimits(String),
	WrongSize(String),
	VolumeOutOfRange,
	GridPointsAtMissingProbe(u16, u32),
}

impl std::fmt::Display for FileError
{
	fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result
	{
		match self {
			Self::OutOfDate => {
				write!(
					formatter,
					"is out of date or not a probe file, rebake the probes"
				)
			}
			Self::PastLimits(message) | Self::WrongSize(message) => formatter.write_str(message),
			Self::VolumeOutOfRange => {
				write!(
					formatter,
					"has a volume that runs past the probes it holds, rebake the probes"
				)
			}
			Self::GridPointsAtMissingProbe(probe, count) => {
				write!(
					formatter,
					"points at probe {probe} of {count}, rebake the probes"
				)
			}
		}
	}
}

/// Checks that a probe file makes sense before any of it is used: its header, how large it is, and
/// that the volumes and grid only refer to what the file holds.
pub fn validate_probe_file(
	header: &FileHeader,
	file_size: u64,
	volumes: &[VolumeData],
	ranges: &[VolumeRange],
	grid: &[u16],
) -> Result<(), FileError>
{
	if !layout_readable(&header.layout) {
		return Err(FileError::OutOfDate);
	}

	if header.volume_count > MAX_VOLUMES
		|| header.probe_count > MAX_PROBES
		|| header.grid_point_count > MAX_GRID_POINTS
	{
		return Err(FileError::PastLimits(format!(
			"holds {} volumes, {} probes and {} grid points, past the limits of {MAX_VOLUMES}, {MAX_PROBES} and {MAX_GRID_POINTS}",
			header.volume_count, header.probe_count, header.grid_point_count
		)));
	}

	let expected = probe_file_size(header.probe_count, header.grid_point_count);

	if file_size != expected {
		return Err(FileError::WrongSize(format!(
			"is {file_size} bytes, not the {expected} its {} probes need",
			header.probe_count
		)));
	}

	for (volume, range) in volumes
		.iter()
		.zip(ranges)
		.take(header.volume_count as usize)
	{
		let dims = volume.dims_and_first;
		let last_point =
			u64::from(dims[3]) + u64::from(dims[0]) * u64::from(dims[1]) * u64::from(dims[2]);
		let last_probe = u64::from(range.first_probe) + u64::from(range.probe_count);

		if last_point > u64::from(header.grid_point_count)
			|| last_probe > u64::from(header.probe_count)
		{
			return Err(FileError::VolumeOutOfRange);
		}
	}

	for probe in grid {
		if *probe != GRID_EMPTY && u32::from(*probe) >= header.probe_count {
			return Err(FileError::GridPointsAtMissingProbe(
				*probe,
				header.probe_count,
			));
		}
	}

	Ok(())
}

pub fn reflection_file_size(probe_count: u32) -> u64
{
	size_of::<ReflectionFileHeader>() as u64
		+ u64::from(probe_count) * (REFLECTION_PROBE_BYTES + REFLECTION_HALFS_PER_PROBE * 2)
}

pub fn validate_reflection_file(
	header: &ReflectionFileHeader,
	file_size: u64,
) -> Result<(), FileError>
{
	let expected = ReflectionFileHeader::default();

	if header.magic != expected.magic
		|| header.version != expected.version
		|| header.size != expected.size
		|| header.mips != expected.mips
		|| header.probe_count > MAX_REFLECTION_PROBES
	{
		return Err(FileError::OutOfDate);
	}

	let wanted = reflection_file_size(header.probe_count);

	if file_size != wanted {
		return Err(FileError::WrongSize(format!(
			"is {file_size} bytes, not the {wanted} its {} probes need",
			header.probe_count
		)));
	}

	Ok(())
}

/// Fills in the far corner of a volume and the volume of one of its cells, which the shader needs.
pub fn finalise_volume_bounds(volume: &mut VolumeData)
{
	if volume.dims_and_first[..3].contains(&0) {
		volume.max_and_cell_volume = [0.0, 0.0, 0.0, f32::MAX];
		return;
	}

	let mut cell_volume = 1.0;

	for axis in 0..3 {
		let cell_size = 1.0 / volume.inv_cell_size[axis].max(1e-6);
		let cells = (volume.dims_and_first[axis] - 1) as f32;

		volume.max_and_cell_volume[axis] = volume.min_and_count[axis] + cell_size * cells;
		cell_volume *= cell_size;
	}

	volume.max_and_cell_volume[3] = cell_volume;
}

/// The shader's description of a volume of `dims` probes whose first grid point is `first_point`.
pub fn volume_data(
	volume_min: Vec3f,
	volume_size: Vec3f,
	dims: [u32; 3],
	first_point: u32,
) -> VolumeData
{
	let min = volume_min.to_array();
	let size = volume_size.to_array();

	let mut volume = VolumeData {
		min_and_count: [min[0], min[1], min[2], 0.0],
		dims_and_first: [dims[0], dims[1], dims[2], first_point],
		..Default::default()
	};

	for axis in 0..3 {
		let cells = (dims[axis] - 1) as f32;

		volume.inv_cell_size[axis] = if size[axis] > 1e-4 {
			cells / size[axis]
		} else {
			1.0
		};
	}

	finalise_volume_bounds(&mut volume);

	volume
}

fn encode_unorm16(value: f32) -> u16
{
	(value.clamp(0.0, 1.0) * 65535.0 + 0.5) as u16
}

/// One row of the depth moments atlas as 16 bit texels. Each probe's moments are the mean distance
/// and mean squared distance of every texel, which are stored as the mean and standard deviation,
/// as at 16 bits the shader would lose the variance to cancellation. Texels of probes past
/// `probe_count` read as unoccluded.
pub fn moments_atlas_row(
	infos: &[f32],
	stride: usize,
	moments_offset: usize,
	probe_count: u32,
	atlas_row: u32,
) -> Vec<u16>
{
	let row_texels = (ATLAS_WIDTH * DEPTH_SIZE) as usize;

	let mut row = vec![0xFFFFu16; row_texels * 2];

	for column in 0..ATLAS_COLUMNS {
		let probe = atlas_row * ATLAS_COLUMNS + column;

		if probe >= probe_count {
			break;
		}

		let moments = &infos[probe as usize * stride + moments_offset..];
		let strip_x = column * ATLAS_PROBE_WIDTH;

		for face in 0..DEPTH_FACES {
			for y in 0..DEPTH_SIZE {
				for x in 0..DEPTH_SIZE {
					let source = ((face * DEPTH_TEXELS_PER_FACE + y * DEPTH_SIZE + x) * 2) as usize;
					let target = ((y * ATLAS_WIDTH + strip_x + face * DEPTH_SIZE + x) * 2) as usize;

					let mean = moments[source];
					let deviation = (moments[source + 1] - mean * mean).max(0.0).sqrt();

					row[target] = encode_unorm16(mean / DEPTH_MAX_DISTANCE);
					row[target + 1] = encode_unorm16(deviation / DEPTH_MAX_DISTANCE);
				}
			}
		}
	}

	row
}

/// Bilinearly samples an RGB capture face of `size` texels square at a point in normalised device
/// coordinates.
pub fn sample_capture_face(face: &[f32], size: u32, ndc_x: f32, ndc_y: f32) -> [f32; 3]
{
	let max_coord = (size - 1) as f32;

	let x = ((ndc_x * 0.5 + 0.5) * size as f32 - 0.5).clamp(0.0, max_coord);
	let y = ((ndc_y * 0.5 + 0.5) * size as f32 - 0.5).clamp(0.0, max_coord);

	let (x0, y0) = (x as u32, y as u32);
	let x1 = (x0 + 1).min(size - 1);
	let y1 = (y0 + 1).min(size - 1);

	let (fx, fy) = (x - x0 as f32, y - y0 as f32);

	let texel = |tx: u32, ty: u32| (ty * size + tx) as usize * 3;
	let (t00, t10, t01, t11) = (texel(x0, y0), texel(x1, y0), texel(x0, y1), texel(x1, y1));

	std::array::from_fn(|channel| {
		let top = face[t00 + channel] + (face[t10 + channel] - face[t00 + channel]) * fx;
		let bottom = face[t01 + channel] + (face[t11 + channel] - face[t01 + channel]) * fx;

		top + (bottom - top) * fy
	})
}

pub const REFLECTION_BLEND_DISTANCE: f32 = 0.25;
pub const REFLECTION_MIN_HALF_EXTENT: f32 = 0.05;
pub const REFLECTION_RELOCATION: f32 = 0.9;

pub struct ReflectionBox
{
	pub box_to_world: Mat4f,
	pub half_extent: Vec3f,
	pub volume: f32,
}

/// The box a reflection probe brush covers, as the matrix that takes the unit cube to it.
pub fn reflection_box(local_min: Vec3f, local_max: Vec3f, local_to_world: &Mat4f) -> ReflectionBox
{
	let half_extent =
		((local_max - local_min) * 0.5).max(&Vec3f::splat(REFLECTION_MIN_HALF_EXTENT));
	let center = (local_max + local_min) * 0.5;

	ReflectionBox {
		box_to_world: Mat4f::as_scale(half_extent)
			* Mat4f::as_translation(center)
			* *local_to_world,
		half_extent,
		volume: half_extent.x * half_extent.y * half_extent.z,
	}
}

/// The probe data the shader reads for a reflection box, captured from `position`.
pub fn reflection_probe_data(
	reflection: &ReflectionBox,
	position: Vec3f,
) -> ([f32; 16], [f32; 4], [f32; 4])
{
	let extent = reflection.half_extent.to_array();

	(
		reflection.box_to_world.inverse().to_rows(),
		[position.x, position.y, position.z, 0.0],
		[
			extent[0] / REFLECTION_BLEND_DISTANCE,
			extent[1] / REFLECTION_BLEND_DISTANCE,
			extent[2] / REFLECTION_BLEND_DISTANCE,
			0.0,
		],
	)
}

pub const BASE_SPACING: f32 = 2.0;
pub const BASE_GRID_BUDGET: u32 = 512;
pub const MAX_SPACING: f32 = 64.0;
pub const SPACING_STEP: f32 = 1.25;
const BASE_SPACING_STEP: f32 = 1.5;

/// The grid of the volume that covers the whole level coarsely, with spacing raised until it fits
/// its budget.
pub fn base_grid(level_size: Vec3f) -> [u32; 3]
{
	let count = |dims: [u32; 3]| dims[0] * dims[1] * dims[2];

	let mut spacing = BASE_SPACING;
	let mut dims = grid_for_spacing(level_size, spacing, true);

	while count(dims) > BASE_GRID_BUDGET && spacing <= MAX_SPACING {
		spacing *= BASE_SPACING_STEP;
		dims = grid_for_spacing(level_size, spacing, true);
	}

	dims
}

#[derive(Debug, PartialEq)]
pub enum SurfaceCandidate
{
	/// The grid has more points than there are points left
	TooManyPoints,
	/// More probes would be needed than there are probes left
	TooManyProbes,
	Fits
	{
		volume_min: Vec3f,
		volume_size: Vec3f,
		dims: [u32; 3],
		needed: u32,
	},
}

/// What a volume over a region at one spacing would be, and whether it fits what is left.
pub fn surface_candidate(
	boxes: &PlacementBoxes,
	region_min: Vec3f,
	region_max: Vec3f,
	spacing: f32,
	cell_centred: bool,
	probe_budget: u32,
	point_budget: u32,
) -> SurfaceCandidate
{
	let dims = grid_for_spacing(region_max - region_min, spacing, cell_centred);

	if dims[0] * dims[1] * dims[2] > point_budget {
		return SurfaceCandidate::TooManyPoints;
	}

	let (volume_min, volume_size) = layout_grid(region_min, region_max, dims, cell_centred);

	let (needed, _) = find_needed_grid_points(
		volume_min,
		grid_cell_size(volume_size, dims),
		dims,
		boxes,
		Fill::Surface,
	);

	if needed > probe_budget {
		return SurfaceCandidate::TooManyProbes;
	}

	SurfaceCandidate::Fits {
		volume_min,
		volume_size,
		dims,
		needed,
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn header(probes: u32, points: u32) -> FileHeader
	{
		FileHeader {
			layout: FileLayout::default(),
			volume_count: 1,
			probe_count: probes,
			grid_point_count: points,
		}
	}

	fn volumes() -> ([VolumeData; 8], [VolumeRange; 8])
	{
		let mut volumes = [VolumeData::default(); 8];
		let mut ranges = [VolumeRange::default(); 8];

		volumes[0].dims_and_first = [2, 2, 2, 0];
		ranges[0] = VolumeRange {
			first_probe: 0,
			probe_count: 4,
		};

		(volumes, ranges)
	}

	#[test]
	fn the_file_sizes_match_what_the_cpp_structs_add_up_to()
	{
		assert_eq!(probe_file_size(0, 0), 32 + 8 * 72);
		assert_eq!(probe_file_size(1, 0) - probe_file_size(0, 0), 144 + 12304);
		assert_eq!(probe_file_size(0, 10) - probe_file_size(0, 0), 20);
		assert_eq!(reflection_file_size(0), 20);
		assert_eq!(
			reflection_file_size(1) - 20,
			96 + REFLECTION_HALFS_PER_PROBE * 2
		);
	}

	#[test]
	fn a_good_file_passes_and_each_fault_is_found()
	{
		let (volumes, ranges) = volumes();
		let grid = vec![0, 1, 2, 3, GRID_EMPTY, GRID_EMPTY, GRID_EMPTY, GRID_EMPTY];
		let size = probe_file_size(4, 8);

		assert!(validate_probe_file(&header(4, 8), size, &volumes, &ranges, &grid).is_ok());

		let mut stale = header(4, 8);
		stale.layout.version = 1;

		assert_eq!(
			validate_probe_file(&stale, size, &volumes, &ranges, &grid),
			Err(FileError::OutOfDate)
		);

		assert!(matches!(
			validate_probe_file(&header(MAX_PROBES + 1, 8), size, &volumes, &ranges, &grid),
			Err(FileError::PastLimits(_))
		));
		assert!(matches!(
			validate_probe_file(&header(4, 8), size + 1, &volumes, &ranges, &grid),
			Err(FileError::WrongSize(_))
		));

		let mut runs_past = volumes;
		runs_past[0].dims_and_first = [2, 2, 3, 0];

		assert_eq!(
			validate_probe_file(&header(4, 8), size, &runs_past, &ranges, &grid),
			Err(FileError::VolumeOutOfRange)
		);

		let bad_grid = vec![0, 1, 2, 9, GRID_EMPTY, GRID_EMPTY, GRID_EMPTY, GRID_EMPTY];

		assert_eq!(
			validate_probe_file(&header(4, 8), size, &volumes, &ranges, &bad_grid),
			Err(FileError::GridPointsAtMissingProbe(9, 4))
		);
	}

	#[test]
	fn a_reflection_file_needs_its_header_and_size_to_match()
	{
		let header = ReflectionFileHeader {
			probe_count: 2,
			..Default::default()
		};

		assert!(validate_reflection_file(&header, reflection_file_size(2)).is_ok());
		assert!(validate_reflection_file(&header, reflection_file_size(2) + 1).is_err());

		let stale = ReflectionFileHeader {
			version: 9,
			..header
		};

		assert_eq!(
			validate_reflection_file(&stale, reflection_file_size(2)),
			Err(FileError::OutOfDate)
		);
	}

	#[test]
	fn a_volume_knows_its_far_corner_and_the_volume_of_a_cell()
	{
		let volume = volume_data(
			Vec3f::new(1.0, 2.0, 3.0),
			Vec3f::new(4.0, 6.0, 8.0),
			[5, 4, 3],
			100,
		);

		assert_eq!(volume.dims_and_first, [5, 4, 3, 100]);
		assert_eq!(volume.max_and_cell_volume[0], 5.0);
		assert_eq!(volume.max_and_cell_volume[1], 8.0);
		assert_eq!(volume.max_and_cell_volume[2], 11.0);
		assert!((volume.max_and_cell_volume[3] - 1.0 * 2.0 * 4.0).abs() < 1e-4);
	}

	#[test]
	fn a_flat_volume_keeps_a_unit_inverse_cell_size_on_that_axis()
	{
		let volume = volume_data(Vec3f::ZERO, Vec3f::new(2.0, 0.0, 2.0), [2, 2, 2], 0);

		assert_eq!(volume.inv_cell_size[1], 1.0);
	}

	#[test]
	fn an_empty_volume_has_no_extent()
	{
		let mut volume = VolumeData::default();

		finalise_volume_bounds(&mut volume);

		assert_eq!(volume.max_and_cell_volume, [0.0, 0.0, 0.0, f32::MAX]);
	}

	#[test]
	fn the_moments_atlas_stores_mean_and_deviation_and_leaves_empty_probes_unoccluded()
	{
		let stride = 4 + DEPTH_FLOAT_COUNT as usize;
		let mut infos = vec![0.0f32; stride];

		for texel in 0..(DEPTH_FACES * DEPTH_TEXELS_PER_FACE) as usize {
			infos[4 + texel * 2] = 10.0;
			infos[4 + texel * 2 + 1] = 10.0 * 10.0 + 25.0;
		}

		let row = moments_atlas_row(&infos, stride, 4, 1, 0);

		assert_eq!(row[0], encode_unorm16(10.0 / DEPTH_MAX_DISTANCE));
		assert_eq!(row[1], encode_unorm16(5.0 / DEPTH_MAX_DISTANCE));

		let second_probe = (ATLAS_PROBE_WIDTH * 2) as usize;

		assert_eq!(row[second_probe], 0xFFFF);
	}

	#[test]
	fn sampling_a_capture_face_blends_between_texels_and_clamps_at_the_edge()
	{
		let face = [0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0];

		assert_eq!(sample_capture_face(&face, 2, -1.0, -1.0), [0.0, 0.0, 0.0]);
		assert_eq!(sample_capture_face(&face, 2, 1.0, 1.0), [1.0, 1.0, 1.0]);

		let middle = sample_capture_face(&face, 2, 0.0, 0.0);

		assert!((middle[0] - 0.5).abs() < 1e-6);
	}

	#[test]
	fn a_reflection_box_has_a_minimum_extent_and_inverts_to_the_unit_cube()
	{
		let reflection = reflection_box(
			Vec3f::new(-1.0, -1.0, -1.0),
			Vec3f::new(1.0, 1.0, 1.0),
			&Mat4f::identity(),
		);

		assert_eq!(reflection.half_extent.to_array(), [1.0, 1.0, 1.0]);

		let thin = reflection_box(Vec3f::ZERO, Vec3f::new(2.0, 0.0, 2.0), &Mat4f::identity());

		assert_eq!(thin.half_extent.y, REFLECTION_MIN_HALF_EXTENT);

		let (world_to_box, position, fade) =
			reflection_probe_data(&reflection, Vec3f::new(1.0, 2.0, 3.0));

		assert_eq!(world_to_box[0], 1.0);
		assert_eq!(position, [1.0, 2.0, 3.0, 0.0]);
		assert_eq!(fade, [4.0, 4.0, 4.0, 0.0]);
	}

	#[test]
	fn the_base_grid_fits_its_budget_by_raising_the_spacing()
	{
		let dims = base_grid(Vec3f::new(150.0, 15.0, 150.0));

		assert!(dims[0] * dims[1] * dims[2] <= BASE_GRID_BUDGET);
		assert!(dims.iter().all(|dim| *dim >= 2));
	}

	#[test]
	fn a_surface_volume_over_an_empty_region_reports_what_does_not_fit()
	{
		let boxes = PlacementBoxes::default();

		let fits = surface_candidate(
			&boxes,
			Vec3f::ZERO,
			Vec3f::splat(10.0),
			5.0,
			false,
			100,
			100,
		);

		assert!(matches!(
			fits,
			SurfaceCandidate::Fits {
				dims: [3, 3, 3],
				..
			}
		));
		assert_eq!(
			surface_candidate(&boxes, Vec3f::ZERO, Vec3f::splat(10.0), 5.0, false, 100, 10),
			SurfaceCandidate::TooManyPoints
		);
		assert_eq!(
			surface_candidate(&boxes, Vec3f::ZERO, Vec3f::splat(10.0), 5.0, false, 10, 100),
			SurfaceCandidate::TooManyProbes
		);
	}
}
