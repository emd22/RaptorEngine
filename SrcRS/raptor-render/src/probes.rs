use std::path::PathBuf;

use raptor_core::{cvar, log_error, log_info, log_warn};
use raptor_entity::{CameraCore, ProjectionKind};
use raptor_math::{Mat4f, Vec3f};

use crate::probe_capture::{CaptureTable, FACES, RADIANCE_CLAMP, half_to_float, sky_gradient_sh};
use crate::probe_data::{
	self, ATLAS_COLUMNS, CACHE_FILE_VERSION, DEPTH_FLOAT_COUNT, FileHeader, MAX_PROBES,
	MAX_REFLECTION_PROBES, MAX_VOLUMES, REFLECTION_HALFS_PER_PROBE, REFLECTION_SIZE, ReflectionBox,
	ReflectionFileHeader, SurfaceCandidate, VolumeData, VolumeRange,
};
use crate::probe_placement::{
	Fill, PlacementBoxes, find_valid_probe_position, is_probe_placement_valid, place_volume,
};
use crate::reflection_filter;

pub const CAPTURE_SIZE: u32 = 64;
pub const PROBES_PER_FRAME: u32 = 4;
const CAPTURE_NEAR_PLANE: f32 = 0.1;
const DEFAULT_BOUNCES: i64 = 3;
const MAX_BOUNCES: i64 = 8;
const DEFAULT_PROBE_SPACING: f32 = 2.5;
const DEFAULT_LEVEL_SPACING: f32 = 0.5;
const MAX_PROBE_SPACING: f32 = 64.0;
const SPACING_STEP: f32 = 1.25;
const GRID_BYTES_STRIDE: usize = 2;

pub type ProbeSh = [[f32; 4]; 9];

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ProbeInfo {
	pub position: [f32; 4],
	pub moments: [f32; DEPTH_FLOAT_COUNT as usize],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ReflectionProbeData {
	pub world_to_box: [f32; 16],
	pub position_and_count: [f32; 4],
	pub fade: [f32; 4],
}

const _: () = assert!(size_of::<ReflectionProbeData>() == 96);
const _: () = assert!(size_of::<ProbeInfo>() == (DEPTH_FLOAT_COUNT as usize + 4) * 4);

fn bytes_of<T>(value: &T) -> &[u8] {
	// SAFETY: only used on repr(C) plain-old-data made of f32 and u32 fields with no padding.
	unsafe { std::slice::from_raw_parts(std::ptr::from_ref(value).cast::<u8>(), size_of::<T>()) }
}

fn slice_bytes<T>(values: &[T]) -> &[u8] {
	// SAFETY: as `bytes_of`, for a slice of such values.
	unsafe {
		std::slice::from_raw_parts(values.as_ptr().cast::<u8>(), std::mem::size_of_val(values))
	}
}

fn read_pod<T: Copy>(bytes: &[u8], at: &mut usize) -> Option<T> {
	let end = *at + size_of::<T>();
	let source = bytes.get(*at..end)?;
	*at = end;

	// SAFETY: `source` holds `size_of::<T>()` bytes, and `T` is plain old data for which every bit
	// pattern is valid.
	Some(unsafe { std::ptr::read_unaligned(source.as_ptr().cast::<T>()) })
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CaptureKind {
	Irradiance,
	Reflection,
}

pub trait ProbeGpu {
	fn wait_idle(&mut self);
	fn write_volumes(&mut self, bytes: &[u8]);
	fn write_grid(&mut self, bytes: &[u8]);
	fn write_probes(&mut self, offset: u64, bytes: &[u8]);
	fn write_moments_row(&mut self, atlas_row: u32, texels: &[u16]);
	fn write_reflection_probes(&mut self, bytes: &[u8]);
	fn write_reflection_cubemap(&mut self, probe: u32, halfs: &[u16]);
	fn create_capture_resources(&mut self);
	fn copy_capture_to_staging(&mut self, kind: CaptureKind, slot: u32, face: u32);
	fn read_capture_color(&mut self, kind: CaptureKind, slot: u32, face: u32) -> Option<Vec<u16>>;
	fn read_capture_depth(&mut self, slot: u32, face: u32) -> Option<Vec<f32>>;
}

pub struct VolumeBrush {
	pub name: String,
	pub min: Vec3f,
	pub max: Vec3f,
}

pub trait ProbeScene {
	fn placement_boxes(&self) -> PlacementBoxes;
	fn volume_brushes(&self) -> Vec<VolumeBrush>;
	fn reflection_boxes(&self) -> Vec<ReflectionBox>;
	fn probe_file_path(&self) -> PathBuf;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum BakeState {
	Idle,
	CapturePending,
	CaptureRecorded,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum BakePhase {
	Irradiance,
	Reflection,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProbeFill {
	Dense,
	Surface,
}

impl ProbeFill {
	fn placement(self) -> Fill {
		match self {
			ProbeFill::Dense => Fill::Dense,
			ProbeFill::Surface => Fill::Surface,
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProbeGridSize {
	pub x: u32,
	pub y: u32,
	pub z: u32,
}

impl Default for ProbeGridSize {
	fn default() -> Self {
		Self { x: 16, y: 8, z: 16 }
	}
}

impl ProbeGridSize {
	pub fn count(&self) -> u32 {
		self.x * self.y * self.z
	}

	pub fn is_valid(&self) -> bool {
		self.x >= 2 && self.y >= 2 && self.z >= 2
	}

	fn dims(&self) -> [u32; 3] {
		[self.x, self.y, self.z]
	}
}

pub fn capture_camera(position: Vec3f, face: usize) -> CameraCore {
	const DIRECTIONS: [[f32; 3]; 6] = [
		[1.0, 0.0, 0.0],
		[-1.0, 0.0, 0.0],
		[0.0, 1.0, 0.0],
		[0.0, -1.0, 0.0],
		[0.0, 0.0, 1.0],
		[0.0, 0.0, -1.0],
	];
	const UPS: [[f32; 3]; 6] = [
		[0.0, 1.0, 0.0],
		[0.0, 1.0, 0.0],
		[0.0, 0.0, 1.0],
		[0.0, 0.0, 1.0],
		[0.0, 1.0, 0.0],
		[0.0, 1.0, 0.0],
	];

	let mut camera = CameraCore::new(ProjectionKind::Perspective);

	camera.z_near = 200.0;
	camera.z_far = CAPTURE_NEAR_PLANE;
	camera.fov_rad = std::f32::consts::FRAC_PI_2;
	camera.aspect = 1.0;
	camera.update();

	let [x, y, z] = position.to_array();
	camera.position = [x, y, z, 0.0];
	camera.view = Mat4f::look_at(
		position,
		position + Vec3f::from_array(DIRECTIONS[face]),
		Vec3f::from_array(UPS[face]),
	)
	.to_rows();
	camera.update_camera_matrix();

	camera
}

fn reset_depth_moments(info: &mut ProbeInfo) {
	for texel in 0..(DEPTH_FLOAT_COUNT / 2) as usize {
		info.moments[texel * 2] = probe_data::DEPTH_MAX_DISTANCE;
		info.moments[texel * 2 + 1] =
			probe_data::DEPTH_MAX_DISTANCE * probe_data::DEPTH_MAX_DISTANCE;
	}
}

fn new_info() -> Box<ProbeInfo> {
	let mut info = Box::new(ProbeInfo {
		position: [0.0; 4],
		moments: [0.0; DEPTH_FLOAT_COUNT as usize],
	});

	reset_depth_moments(&mut info);

	info
}

pub struct ProbeManager {
	gpu: Box<dyn ProbeGpu>,
	probes: Vec<ProbeSh>,
	infos: Vec<Box<ProbeInfo>>,
	volumes: [VolumeData; MAX_VOLUMES as usize],
	ranges: [VolumeRange; MAX_VOLUMES as usize],
	volume_count: u32,
	probe_count: u32,
	grid_probes: Vec<u16>,
	grid_point_count: u32,
	grid_dirty: bool,
	bake_state: BakeState,
	bake_phase: BakePhase,
	capturing_faces: bool,
	current_probe: u32,
	batch_start: u32,
	current_bounce: u32,
	bounce_count: u32,
	reflection_probes: [ReflectionProbeData; MAX_REFLECTION_PROBES as usize],
	reflection_count: u32,
	reflections_baked: bool,
	reflection_texels: Vec<u16>,
	capture_table: Option<CaptureTable>,
	capture_face_to_clip: [Mat4f; FACES],
	capture_ready: bool,
}

impl ProbeManager {
	pub fn new(gpu: Box<dyn ProbeGpu>) -> Self {
		let sky = sky_gradient_sh([0.055, 0.062, 0.075], [0.025, 0.022, 0.020]);

		let mut manager = Self {
			gpu,
			probes: vec![sky; MAX_PROBES as usize],
			infos: (0..MAX_PROBES).map(|_| new_info()).collect(),
			volumes: [VolumeData::default(); MAX_VOLUMES as usize],
			ranges: [VolumeRange::default(); MAX_VOLUMES as usize],
			volume_count: 0,
			probe_count: 0,
			grid_probes: vec![probe_data::GRID_EMPTY; probe_data::MAX_GRID_POINTS as usize],
			grid_point_count: 0,
			grid_dirty: true,
			bake_state: BakeState::Idle,
			bake_phase: BakePhase::Irradiance,
			capturing_faces: false,
			current_probe: 0,
			batch_start: 0,
			current_bounce: 0,
			bounce_count: 1,
			reflection_probes: [ReflectionProbeData::default(); MAX_REFLECTION_PROBES as usize],
			reflection_count: 0,
			reflections_baked: false,
			reflection_texels: Vec::new(),
			capture_table: None,
			capture_face_to_clip: [Mat4f::identity(); FACES],
			capture_ready: false,
		};

		manager.clear_volumes();

		manager.add_volume_and_place(
			Vec3f::new(-20.0, -2.0, -20.0),
			Vec3f::new(150.0, 15.0, 150.0),
			ProbeGridSize { x: 4, y: 2, z: 4 },
			&PlacementBoxes::default(),
			ProbeFill::Dense,
		);

		manager.upload_reflection_probes(0);

		manager
	}

	pub fn probe_count(&self) -> u32 {
		self.probe_count
	}

	pub fn probe_position(&self, index: u32) -> Vec3f {
		let [x, y, z, _] = self.infos[index as usize].position;

		Vec3f::new(x, y, z)
	}

	pub fn grid_point_count(&self) -> u32 {
		self.grid_point_count
	}

	pub fn current_probe_index(&self) -> u32 {
		if self.bake_phase == BakePhase::Irradiance {
			self.current_probe
		} else {
			u32::MAX
		}
	}

	pub fn volume_count(&self) -> u32 {
		self.volume_count
	}

	pub fn volume_of_probe(&self, probe: u32) -> u32 {
		(0..self.volume_count)
			.find(|&volume| {
				let range = self.ranges[volume as usize];

				probe >= range.first_probe && probe < range.first_probe + range.probe_count
			})
			.unwrap_or(MAX_VOLUMES)
	}

	pub fn volume_probe_range(&self, volume: u32) -> VolumeRange {
		self.ranges[volume as usize]
	}

	pub fn reflection_probe_count(&self) -> u32 {
		self.reflection_count
	}

	pub fn reflection_probe_position(&self, index: u32) -> Vec3f {
		let [x, y, z, _] = self.reflection_probes[index as usize].position_and_count;

		Vec3f::new(x, y, z)
	}

	pub fn reflections_baked(&self) -> bool {
		self.reflections_baked
	}

	pub fn is_baking(&self) -> bool {
		self.bake_state != BakeState::Idle
	}

	pub fn is_baking_reflections(&self) -> bool {
		self.is_baking() && self.bake_phase == BakePhase::Reflection
	}

	pub fn is_capture_pending(&self) -> bool {
		self.bake_state == BakeState::CapturePending
	}

	pub fn is_capturing_faces(&self) -> bool {
		self.capturing_faces
	}

	pub fn is_capturing_bounce(&self) -> bool {
		self.capturing_faces
			&& (self.current_bounce > 0 || self.bake_phase == BakePhase::Reflection)
	}

	pub fn is_capturing_reflection(&self) -> bool {
		self.capturing_faces && self.bake_phase == BakePhase::Reflection
	}

	pub fn capture_extent(&self) -> [u32; 2] {
		let size = if self.bake_phase == BakePhase::Reflection {
			REFLECTION_SIZE
		} else {
			CAPTURE_SIZE
		};

		[size, size]
	}

	pub fn clear_volumes(&mut self) {
		if self.is_baking() {
			log_warn!("Cannot change the probe volumes while they are baking");
			return;
		}

		self.volume_count = 0;
		self.probe_count = 0;
		self.grid_point_count = 0;
		self.grid_dirty = true;

		self.refresh_volume_counts();
		self.upload_to_gpu(0, 0);
	}

	fn refresh_volume_counts(&mut self) {
		for volume in &mut self.volumes {
			volume.min_and_count[3] = self.volume_count as f32;
		}
	}

	pub fn add_volume(
		&mut self,
		scene: &dyn ProbeScene,
		center: Vec3f,
		size: Vec3f,
		grid: ProbeGridSize,
	) -> bool {
		if self.is_baking() {
			log_warn!("Cannot add a probe volume while the probes are baking");
			return false;
		}

		self.add_volume_and_place(
			center - size * 0.5,
			size,
			grid,
			&scene.placement_boxes(),
			ProbeFill::Dense,
		)
	}

	pub fn add_level_volumes(&mut self, scene: &dyn ProbeScene) -> bool {
		if self.is_baking() {
			log_warn!("Cannot add a probe volume while the probes are baking");
			return false;
		}

		let boxes = scene.placement_boxes();

		if boxes.is_empty() {
			log_error!("Cannot fit a probe volume to the level: there is no geometry to fit it to");
			return false;
		}

		let base = self.add_level_base_volume(&boxes);
		let surface = self.add_level_surface_volume(&boxes);

		base && surface
	}

	fn add_level_base_volume(&mut self, boxes: &PlacementBoxes) -> bool {
		let dims = probe_data::base_grid(boxes.max() - boxes.min());
		let grid = ProbeGridSize {
			x: dims[0],
			y: dims[1],
			z: dims[2],
		};

		let (volume_min, volume_size) =
			crate::probe_placement::layout_grid(boxes.min(), boxes.max(), dims, true);

		self.add_volume_and_place(volume_min, volume_size, grid, boxes, ProbeFill::Dense)
	}

	fn add_level_surface_volume(&mut self, boxes: &PlacementBoxes) -> bool {
		let spacing = cvar::float("r_probe_level_spacing", DEFAULT_LEVEL_SPACING);

		self.add_surface_volume_for_budget(boxes.min(), boxes.max(), spacing, boxes, true)
	}

	fn add_surface_volume_for_budget(
		&mut self,
		region_min: Vec3f,
		region_max: Vec3f,
		spacing: f32,
		boxes: &PlacementBoxes,
		cell_centred: bool,
	) -> bool {
		if self.volume_count >= MAX_VOLUMES {
			log_error!("Probe volume rejected: all {MAX_VOLUMES} volume slots are taken");
			return false;
		}

		let probe_budget = MAX_PROBES - self.probe_count;
		let point_budget = probe_data::MAX_GRID_POINTS - self.grid_point_count;

		let mut tried = spacing.max(1e-2);

		while tried <= MAX_PROBE_SPACING {
			let candidate = probe_data::surface_candidate(
				boxes,
				region_min,
				region_max,
				tried,
				cell_centred,
				probe_budget,
				point_budget,
			);

			if let SurfaceCandidate::Fits {
				volume_min,
				volume_size,
				dims,
				..
			} = candidate
			{
				let grid = ProbeGridSize {
					x: dims[0],
					y: dims[1],
					z: dims[2],
				};

				if self.add_volume_and_place(
					volume_min,
					volume_size,
					grid,
					boxes,
					ProbeFill::Surface,
				) {
					if tried > spacing {
						log_warn!(
							"Probe volume spacing raised from {spacing}m to {tried}m to fit the {probe_budget} probes left"
						);
					}

					return true;
				}
			}

			tried *= SPACING_STEP;
		}

		log_error!(
			"Probe volume rejected: no spacing up to {MAX_PROBE_SPACING}m fits the {probe_budget} probes left"
		);

		false
	}

	pub fn rebuild_volumes_from_world(&mut self, scene: &dyn ProbeScene) -> u32 {
		if self.is_baking() {
			log_warn!("Cannot rebuild the probe volumes while they are baking");
			return 0;
		}

		self.clear_volumes();

		let boxes = scene.placement_boxes();

		if boxes.is_empty() {
			log_error!("Cannot fit a probe volume to the level: there is no geometry to fit it to");
			return 0;
		}

		self.add_level_base_volume(&boxes);

		let spacing = cvar::float("r_probe_spacing", DEFAULT_PROBE_SPACING);
		let mut brush_volumes = 0;

		for brush in scene.volume_brushes() {
			if self.add_surface_volume_for_budget(brush.min, brush.max, spacing, &boxes, false) {
				brush_volumes += 1;
			} else {
				log_warn!("Probe volume brush '{}' was skipped", brush.name);
			}
		}

		self.add_level_surface_volume(&boxes);

		self.rebuild_reflection_probes_from_world(scene);

		log_info!(
			"Probe volumes rebuilt: {brush_volumes} from editor brushes, {} in total, {} of {MAX_PROBES} probes and {} of {} grid points used",
			self.volume_count,
			self.probe_count,
			self.grid_point_count,
			probe_data::MAX_GRID_POINTS
		);

		brush_volumes
	}

	fn add_volume_and_place(
		&mut self,
		volume_min: Vec3f,
		volume_size: Vec3f,
		grid: ProbeGridSize,
		boxes: &PlacementBoxes,
		fill: ProbeFill,
	) -> bool {
		if !grid.is_valid() {
			log_error!(
				"Probe volume rejected: a {}x{}x{} grid needs at least 2 probes along each axis",
				grid.x,
				grid.y,
				grid.z
			);
			return false;
		}

		if self.volume_count >= MAX_VOLUMES {
			log_error!("Probe volume rejected: all {MAX_VOLUMES} volume slots are taken");
			return false;
		}

		let num_points = grid.count();

		if num_points > probe_data::MAX_GRID_POINTS - self.grid_point_count {
			log_error!(
				"Probe volume rejected: its grid has {num_points} points and only {} of the {} grid point budget are left",
				probe_data::MAX_GRID_POINTS - self.grid_point_count,
				probe_data::MAX_GRID_POINTS
			);
			return false;
		}

		let first_probe = self.probe_count;

		if !self.place_volume_probes(
			self.volume_count,
			volume_min,
			volume_size,
			grid,
			boxes,
			fill,
		) {
			return false;
		}

		self.volume_count += 1;
		self.grid_dirty = true;

		self.refresh_volume_counts();
		self.upload_to_gpu(first_probe, self.probe_count - first_probe);

		true
	}

	fn place_volume_probes(
		&mut self,
		volume_index: u32,
		volume_min: Vec3f,
		volume_size: Vec3f,
		grid: ProbeGridSize,
		boxes: &PlacementBoxes,
		fill: ProbeFill,
	) -> bool {
		let first_probe = self.probe_count;
		let first_point = self.grid_point_count;
		let dims = grid.dims();

		let Ok(placement) = place_volume(
			boxes,
			volume_min,
			volume_size,
			dims,
			fill.placement(),
			MAX_PROBES - first_probe,
		) else {
			log_warn!(
				"Probe volume rejected: it needs more than the {} probes left of the {MAX_PROBES} probe budget",
				MAX_PROBES - first_probe
			);
			return false;
		};

		for (i, position) in placement.positions.iter().enumerate() {
			let info = &mut self.infos[first_probe as usize + i];

			reset_depth_moments(info);

			let [x, y, z] = position.to_array();
			info.position = [x, y, z, 1.0];
		}

		for (point, placed) in placement.grid.iter().enumerate() {
			self.grid_probes[first_point as usize + point] =
				placed.map_or(probe_data::GRID_EMPTY, |index| (first_probe + index) as u16);
		}

		let placed = placement.positions.len() as u32;

		self.volumes[volume_index as usize] =
			probe_data::volume_data(volume_min, volume_size, dims, first_point);
		self.ranges[volume_index as usize] = VolumeRange {
			first_probe,
			probe_count: placed,
		};

		self.probe_count = first_probe + placed;
		self.grid_point_count = first_point + grid.count();

		log_info!(
			"Probe volume {volume_index} ({}): grid={}x{}x{}, {} of {} grid points needed, {placed} probes from index {first_probe} ({} pushed out, {} hugging, {} relocated, {} unplaced)",
			if fill == ProbeFill::Dense {
				"dense"
			} else {
				"surface"
			},
			grid.x,
			grid.y,
			grid.z,
			placement.needed,
			grid.count(),
			placement.pushed,
			placement.hugged,
			placement.relocated,
			placement.unplaced
		);

		true
	}

	pub fn begin_bake(&mut self) {
		if self.is_baking() {
			log_warn!("A probe bake is already in progress");
			return;
		}

		if self.volume_count == 0 || self.probe_count == 0 {
			log_error!("Probe bake failed: no probes have been placed");
			return;
		}

		self.current_probe = 0;
		self.current_bounce = 0;
		self.bake_phase = BakePhase::Irradiance;
		self.bounce_count =
			cvar::int("r_probe_bounces", DEFAULT_BOUNCES).clamp(1, MAX_BOUNCES) as u32;
		self.bake_state = BakeState::CapturePending;

		log_info!(
			"Probe bake started ({} probes across {} volume(s), {PROBES_PER_FRAME} per frame, {} bounce(s))",
			self.probe_count,
			self.volume_count,
			self.bounce_count
		);
	}

	pub fn begin_grid_bake(&mut self, scene: &dyn ProbeScene) {
		if self.is_baking() {
			log_warn!("A probe bake is already in progress");
			return;
		}

		self.clear_volumes();

		if self.add_level_volumes(scene) {
			self.begin_bake();
		}
	}

	pub fn begin_grid_bake_at(
		&mut self,
		scene: &dyn ProbeScene,
		center: Vec3f,
		size: Vec3f,
		grid: ProbeGridSize,
	) {
		if self.is_baking() {
			log_warn!("A probe bake is already in progress");
			return;
		}

		if !grid.is_valid() {
			log_error!("Probe volume rejected: the grid needs at least 2 probes along each axis");
			return;
		}

		self.clear_volumes();

		if self.add_volume(scene, center, size, grid) {
			self.begin_bake();
		}
	}

	fn create_capture_resources(&mut self) {
		if self.capture_ready {
			return;
		}

		self.gpu.create_capture_resources();

		let mut inv_view_projections = [[0.0f32; 16]; FACES];
		let mut inv_projection = [0.0f32; 16];

		for (face, out) in inv_view_projections.iter_mut().enumerate() {
			let camera = capture_camera(Vec3f::ZERO, face);

			let inv_view_projection =
				Mat4f::from_rows(&camera.inv_projection) * Mat4f::from_rows(&camera.inv_view);

			inv_projection = camera.inv_projection;
			self.capture_face_to_clip[face] = inv_view_projection.inverse();
			*out = inv_view_projection.to_rows();
		}

		self.capture_table =
			CaptureTable::new(CAPTURE_SIZE as usize, inv_projection, &inv_view_projections);

		self.capture_ready = true;
	}

	pub fn record_capture_batch(&mut self, render_face: &mut dyn FnMut(&CameraCore, CaptureKind)) {
		if self.bake_state != BakeState::CapturePending {
			return;
		}

		self.create_capture_resources();

		if self.bake_phase == BakePhase::Reflection {
			self.record_reflection_capture(render_face);
			return;
		}

		self.batch_start = self.current_probe;
		let batch_end = (self.current_probe + PROBES_PER_FRAME).min(self.probe_count);

		self.capturing_faces = true;

		while self.current_probe < batch_end {
			let slot = self.current_probe - self.batch_start;
			let position = self.probe_position(self.current_probe);

			for face in 0..FACES {
				let camera = capture_camera(position, face);

				render_face(&camera, CaptureKind::Irradiance);
				self.gpu
					.copy_capture_to_staging(CaptureKind::Irradiance, slot, face as u32);
			}

			self.current_probe += 1;
		}

		self.capturing_faces = false;
		self.bake_state = BakeState::CaptureRecorded;
	}

	fn record_reflection_capture(&mut self, render_face: &mut dyn FnMut(&CameraCore, CaptureKind)) {
		self.batch_start = self.current_probe;

		let position = self.reflection_probe_position(self.current_probe);

		self.capturing_faces = true;

		for face in 0..FACES {
			let camera = capture_camera(position, face);

			render_face(&camera, CaptureKind::Reflection);
			self.gpu
				.copy_capture_to_staging(CaptureKind::Reflection, 0, face as u32);
		}

		self.capturing_faces = false;
		self.current_probe += 1;
		self.bake_state = BakeState::CaptureRecorded;
	}

	pub fn service_capture_bake(&mut self) {
		if self.bake_state != BakeState::CaptureRecorded {
			return;
		}

		self.gpu.wait_idle();

		if self.bake_phase == BakePhase::Reflection {
			self.service_reflection_bake();
			return;
		}

		for probe in self.batch_start..self.current_probe {
			if !self.read_back_probe(probe - self.batch_start, probe) {
				log_error!("Probe grid bake failed: could not read back probe {probe}");
				self.bake_state = BakeState::Idle;
				return;
			}
		}

		self.upload_to_gpu(self.batch_start, self.current_probe - self.batch_start);

		if self.current_probe < self.probe_count {
			self.bake_state = BakeState::CapturePending;
			log_info!(
				"Probe bake progress: {}/{}",
				self.current_probe,
				self.probe_count
			);
			return;
		}

		if self.current_bounce + 1 < self.bounce_count {
			self.current_bounce += 1;
			self.current_probe = 0;
			self.bake_state = BakeState::CapturePending;
			log_info!(
				"Probe bounce {}/{} done, starting the next",
				self.current_bounce,
				self.bounce_count
			);
			return;
		}

		self.bake_state = BakeState::Idle;

		log_info!(
			"Probe bake complete ({} probes across {} volume(s), {} bounce(s))",
			self.probe_count,
			self.volume_count,
			self.bounce_count
		);

		if self.reflection_count > 0 {
			self.start_reflection_phase();
		}
	}

	fn read_back_probe(&mut self, slot: u32, probe: u32) -> bool {
		let mut colors = Vec::with_capacity(FACES);
		let mut depths = Vec::with_capacity(FACES);

		for face in 0..FACES as u32 {
			let color = self
				.gpu
				.read_capture_color(CaptureKind::Irradiance, slot, face);
			let depth = self.gpu.read_capture_depth(slot, face);

			let (Some(color), Some(depth)) = (color, depth) else {
				return false;
			};

			colors.push(color);
			depths.push(depth);
		}

		let Some(table) = &self.capture_table else {
			return false;
		};

		let Some(projection) = table.project(
			std::array::from_fn(|face| colors[face].as_slice()),
			std::array::from_fn(|face| depths[face].as_slice()),
		) else {
			return false;
		};

		self.probes[probe as usize] = projection.sh;
		self.infos[probe as usize].moments = projection.moments;

		true
	}

	fn upload_to_gpu(&mut self, first_probe: u32, count: u32) {
		self.gpu.wait_idle();

		self.gpu.write_volumes(slice_bytes(&self.volumes));

		if self.grid_dirty && self.grid_point_count > 0 {
			let points = self.grid_point_count.div_ceil(2) as usize * 2;

			self.gpu.write_grid(slice_bytes(
				&self.grid_probes[..points.min(self.grid_probes.len())],
			));
		}

		self.grid_dirty = false;

		if count == 0 {
			return;
		}

		for probe in first_probe..first_probe + count {
			let position = self.infos[probe as usize].position;

			for lane in 0..3 {
				self.probes[probe as usize][lane][3] = position[lane];
			}
		}

		let range = first_probe as usize..(first_probe + count) as usize;

		self.gpu.write_probes(
			u64::from(first_probe) * size_of::<ProbeSh>() as u64,
			slice_bytes(&self.probes[range]),
		);

		self.upload_moments_atlas(first_probe, count);
	}

	fn upload_moments_atlas(&mut self, first_probe: u32, count: u32) {
		let first_row = first_probe / ATLAS_COLUMNS;
		let last_row = (first_probe + count - 1) / ATLAS_COLUMNS;

		let stride = size_of::<ProbeInfo>() / 4;
		let moments_offset = 4;

		let mut floats = vec![0.0f32; self.probe_count as usize * stride];

		for (probe, info) in self
			.infos
			.iter()
			.take(self.probe_count as usize)
			.enumerate()
		{
			let base = probe * stride;

			floats[base..base + 4].copy_from_slice(&info.position);
			floats[base + 4..base + stride].copy_from_slice(&info.moments);
		}

		for atlas_row in first_row..=last_row {
			let row = probe_data::moments_atlas_row(
				&floats,
				stride,
				moments_offset,
				self.probe_count,
				atlas_row,
			);

			self.gpu.write_moments_row(atlas_row, &row);
		}
	}

	pub fn save_probes(&mut self, scene: &dyn ProbeScene) -> bool {
		let saved = self.save_irradiance_probes(scene);

		if saved {
			self.save_reflection_probes(scene);
		}

		saved
	}

	pub fn load_probes(&mut self, scene: &dyn ProbeScene) -> bool {
		let loaded = self.load_irradiance_probes(scene);

		if !loaded || !self.load_reflection_probes(scene) {
			self.reflection_count = 0;
			self.reflections_baked = false;
			self.upload_reflection_probes(0);
		}

		loaded
	}

	fn reflection_path(scene: &dyn ProbeScene) -> PathBuf {
		scene.probe_file_path().with_extension("fxrefl")
	}

	fn save_irradiance_probes(&self, scene: &dyn ProbeScene) -> bool {
		if self.is_baking() {
			log_warn!("Cannot save the probes while they are baking");
			return false;
		}

		let path = scene.probe_file_path();

		let header = FileHeader {
			volume_count: self.volume_count,
			probe_count: self.probe_count,
			grid_point_count: self.grid_point_count,
			..FileHeader::default()
		};

		let mut bytes = Vec::new();

		bytes.extend_from_slice(bytes_of(&header));
		bytes.extend_from_slice(slice_bytes(&self.volumes));
		bytes.extend_from_slice(slice_bytes(&self.ranges));
		bytes.extend_from_slice(slice_bytes(
			&self.grid_probes[..self.grid_point_count as usize],
		));
		bytes.extend_from_slice(slice_bytes(&self.probes[..self.probe_count as usize]));

		for info in &self.infos[..self.probe_count as usize] {
			bytes.extend_from_slice(bytes_of(&**info));
		}

		if let Err(error) = std::fs::write(&path, bytes) {
			log_error!("Could not save the probes to {}: {error}", path.display());
			return false;
		}

		log_info!(
			"Saved {} light probes across {} volume(s) to {}",
			self.probe_count,
			self.volume_count,
			path.display()
		);

		true
	}

	fn load_irradiance_probes(&mut self, scene: &dyn ProbeScene) -> bool {
		let path = scene.probe_file_path();

		let Ok(bytes) = std::fs::read(&path) else {
			log_info!(
				"No probe file at {}, using procedural probes",
				path.display()
			);
			return false;
		};

		let mut at = 0;

		let Some(header) = read_pod::<FileHeader>(&bytes, &mut at) else {
			log_warn!(
				"Probe file {} is out of date or not a probe file, rebake the probes",
				path.display()
			);
			return false;
		};

		let mut volumes = [VolumeData::default(); MAX_VOLUMES as usize];
		let mut ranges = [VolumeRange::default(); MAX_VOLUMES as usize];

		let mut grid = Vec::new();

		let parsed = (|| {
			for volume in &mut volumes {
				*volume = read_pod(&bytes, &mut at)?;
			}

			for range in &mut ranges {
				*range = read_pod(&bytes, &mut at)?;
			}

			for _ in 0..header.grid_point_count.min(probe_data::MAX_GRID_POINTS) {
				grid.push(read_pod::<u16>(&bytes, &mut at)?);
			}

			Some(())
		})();

		let checked = probe_data::validate_probe_file(
			&header,
			bytes.len() as u64,
			&volumes,
			&ranges,
			if parsed.is_some() { &grid } else { &[] },
		);

		if let Err(error) = checked {
			log_error!("Probe file {} {error}", path.display());
			return false;
		}

		if parsed.is_none() {
			log_error!("Could not read the probe volumes from {}", path.display());
			return false;
		}

		let mut probes = Vec::with_capacity(header.probe_count as usize);
		let mut infos = Vec::with_capacity(header.probe_count as usize);

		for _ in 0..header.probe_count {
			let Some(probe) = read_pod::<ProbeSh>(&bytes, &mut at) else {
				log_error!("Could not read the probes from {}", path.display());
				return false;
			};

			probes.push(probe);
		}

		for _ in 0..header.probe_count {
			let Some(info) = read_pod::<ProbeInfo>(&bytes, &mut at) else {
				log_error!("Could not read the probes from {}", path.display());
				return false;
			};

			infos.push(Box::new(info));
		}

		self.volumes = volumes;
		self.ranges = ranges;

		for (index, probe) in probes.into_iter().enumerate() {
			self.probes[index] = probe;
		}

		for (index, info) in infos.into_iter().enumerate() {
			self.infos[index] = info;
		}

		self.grid_probes[..grid.len()].copy_from_slice(&grid);
		self.volume_count = header.volume_count;
		self.probe_count = header.probe_count;
		self.grid_point_count = header.grid_point_count;
		self.grid_dirty = true;

		self.refresh_volume_counts();
		self.upload_to_gpu(0, self.probe_count);

		log_info!(
			"Loaded {} light probes across {} volume(s) from {} (format {CACHE_FILE_VERSION})",
			self.probe_count,
			self.volume_count,
			path.display()
		);

		true
	}

	pub fn rebuild_reflection_probes_from_world(&mut self, scene: &dyn ProbeScene) -> u32 {
		if self.is_baking() {
			log_warn!("Cannot rebuild the reflection probes while the probes are baking");
			return 0;
		}

		let boxes = scene.placement_boxes();

		let mut reflection_boxes = scene.reflection_boxes();

		reflection_boxes.sort_by(|a, b| a.volume.total_cmp(&b.volume));

		let brush_probes = reflection_boxes.len();
		let has_level_probe = !boxes.is_empty() && cvar::int("r_reflection_level_probe", 0) != 0;

		let mut max_brush_probes = MAX_REFLECTION_PROBES as usize;

		if has_level_probe {
			max_brush_probes -= 1;
		}

		if reflection_boxes.len() > max_brush_probes {
			log_warn!(
				"{} reflection probe brushes are placed but only {max_brush_probes} fit, the largest are left out",
				reflection_boxes.len()
			);
			reflection_boxes.truncate(max_brush_probes);
		}

		if has_level_probe {
			reflection_boxes.push(probe_data::reflection_box(
				boxes.min(),
				boxes.max(),
				&Mat4f::identity(),
			));
		}

		self.reflection_count = reflection_boxes.len() as u32;
		self.reflections_baked = false;

		for (index, reflection) in reflection_boxes.iter().enumerate() {
			let center = Vec3f::from(reflection.box_to_world.row(3));
			let center = Vec3f::new(center.x, center.y, center.z);

			let max_relocation = reflection.half_extent * probe_data::REFLECTION_RELOCATION;

			let position = if is_probe_placement_valid(center, center, &boxes) {
				center
			} else if let Some(found) =
				find_valid_probe_position(center, center, max_relocation, &boxes)
			{
				found
			} else {
				log_warn!(
					"Reflection probe {index} is captured from inside of the level's geometry, move its brush"
				);
				center
			};

			let (world_to_box, position_and_count, fade) =
				probe_data::reflection_probe_data(reflection, position);

			self.reflection_probes[index] = ReflectionProbeData {
				world_to_box,
				position_and_count,
				fade,
			};
		}

		self.upload_reflection_probes(0);

		log_info!(
			"Reflection probes rebuilt: {} from editor brushes, {} in total",
			brush_probes.min(max_brush_probes),
			self.reflection_count
		);

		self.reflection_count
	}

	pub fn begin_reflection_bake(&mut self) {
		if self.is_baking() {
			log_warn!("A probe bake is already in progress");
			return;
		}

		if self.reflection_count == 0 {
			log_error!("Reflection bake failed: no reflection probes have been placed");
			return;
		}

		self.start_reflection_phase();
	}

	fn start_reflection_phase(&mut self) {
		self.bake_phase = BakePhase::Reflection;
		self.current_probe = 0;
		self.reflections_baked = false;
		self.bake_state = BakeState::CapturePending;

		self.reflection_texels =
			vec![0; self.reflection_count as usize * REFLECTION_HALFS_PER_PROBE as usize];

		self.upload_reflection_probes(0);

		log_info!(
			"Reflection probe bake started ({} probes)",
			self.reflection_count
		);
	}

	fn service_reflection_bake(&mut self) {
		let probe = self.batch_start;

		if !self.read_back_reflection_probe(probe) {
			log_error!("Reflection probe bake failed: could not read back probe {probe}");
			self.bake_phase = BakePhase::Irradiance;
			self.bake_state = BakeState::Idle;
			return;
		}

		self.upload_reflection_cubemap(probe);

		if self.current_probe < self.reflection_count {
			self.bake_state = BakeState::CapturePending;
			log_info!(
				"Reflection probe bake progress: {}/{}",
				self.current_probe,
				self.reflection_count
			);
			return;
		}

		self.bake_phase = BakePhase::Irradiance;
		self.bake_state = BakeState::Idle;
		self.reflections_baked = true;

		self.upload_reflection_probes(self.reflection_count);

		log_info!(
			"Reflection probe bake complete ({} probes)",
			self.reflection_count
		);
	}

	fn read_back_reflection_probe(&mut self, probe: u32) -> bool {
		let size = REFLECTION_SIZE as usize;
		let pixels = size * size;

		let mut captures: Vec<Vec<f32>> = Vec::with_capacity(FACES);

		for face in 0..FACES as u32 {
			let Some(rgba) = self
				.gpu
				.read_capture_color(CaptureKind::Reflection, 0, face)
			else {
				return false;
			};

			if rgba.len() < pixels * 4 {
				return false;
			}

			let mut capture = Vec::with_capacity(pixels * 3);

			for pixel in rgba.chunks_exact(4).take(pixels) {
				for &half in &pixel[..3] {
					capture.push(half_to_float(half).clamp(0.0, RADIANCE_CLAMP));
				}
			}

			captures.push(capture);
		}

		let mut cube: Vec<Vec<f32>> = Vec::with_capacity(FACES);

		for face in 0..FACES as u32 {
			let mut out = Vec::with_capacity(pixels * 3);

			for y in 0..size {
				for x in 0..size {
					let u = ((x as f32 + 0.5) / size as f32) * 2.0 - 1.0;
					let v = ((y as f32 + 0.5) / size as f32) * 2.0 - 1.0;

					let direction = reflection_filter::face_uv_to_direction(face, u, v);
					let (capture_face, _, _) = reflection_filter::direction_to_face_uv(direction);

					let matrix = self.capture_face_to_clip[capture_face as usize].to_rows();
					let point = [direction.x, direction.y, direction.z, 1.0f32];

					let clip: [f32; 4] = std::array::from_fn(|j| {
						point[0] * matrix[j]
							+ point[1] * matrix[4 + j]
							+ point[2] * matrix[8 + j]
							+ point[3] * matrix[12 + j]
					});

					out.extend(probe_data::sample_capture_face(
						&captures[capture_face as usize],
						size as u32,
						clip[0] / clip[3],
						clip[1] / clip[3],
					));
				}
			}

			cube.push(out);
		}

		let filtered = reflection_filter::prefilter(
			&std::array::from_fn(|face| cube[face].as_slice()),
			REFLECTION_SIZE,
			probe_data::REFLECTION_MIPS,
		);

		let per_probe = REFLECTION_HALFS_PER_PROBE as usize;
		let start = probe as usize * per_probe;

		self.reflection_texels[start..start + per_probe].copy_from_slice(&filtered);

		true
	}

	fn upload_reflection_probes(&mut self, visible_count: u32) {
		self.gpu.wait_idle();

		let mut probes = self.reflection_probes;

		for probe in &mut probes {
			probe.position_and_count[3] = visible_count as f32;
			probe.fade[3] = self.reflection_count as f32;
		}

		self.gpu.write_reflection_probes(slice_bytes(&probes));
	}

	fn upload_reflection_cubemap(&mut self, probe: u32) {
		self.gpu.wait_idle();

		let per_probe = REFLECTION_HALFS_PER_PROBE as usize;
		let start = probe as usize * per_probe;

		self.gpu
			.write_reflection_cubemap(probe, &self.reflection_texels[start..start + per_probe]);
	}

	fn save_reflection_probes(&self, scene: &dyn ProbeScene) -> bool {
		if self.reflection_count > 0 && !self.reflections_baked {
			log_warn!("The reflection probes are not baked, so they were not saved");
			return false;
		}

		let path = Self::reflection_path(scene);

		if self.reflection_count == 0 && !path.exists() {
			return true;
		}

		let header = ReflectionFileHeader {
			probe_count: self.reflection_count,
			..ReflectionFileHeader::default()
		};

		let count = self.reflection_count as usize;

		let mut bytes = Vec::new();

		bytes.extend_from_slice(bytes_of(&header));
		bytes.extend_from_slice(slice_bytes(&self.reflection_probes[..count]));
		bytes.extend_from_slice(slice_bytes(
			&self.reflection_texels[..count * REFLECTION_HALFS_PER_PROBE as usize],
		));

		if let Err(error) = std::fs::write(&path, bytes) {
			log_error!(
				"Could not save the reflection probes to {}: {error}",
				path.display()
			);
			return false;
		}

		log_info!("Saved {count} reflection probes to {}", path.display());

		true
	}

	fn load_reflection_probes(&mut self, scene: &dyn ProbeScene) -> bool {
		let path = Self::reflection_path(scene);

		let Ok(bytes) = std::fs::read(&path) else {
			log_info!("No reflection probe file at {}", path.display());
			return false;
		};

		let mut at = 0;

		let Some(header) = read_pod::<ReflectionFileHeader>(&bytes, &mut at) else {
			log_warn!(
				"Reflection probe file {} is out of date or not a reflection probe file, rebake the probes",
				path.display()
			);
			return false;
		};

		if let Err(error) = probe_data::validate_reflection_file(&header, bytes.len() as u64) {
			log_error!("Reflection probe file {} {error}", path.display());
			return false;
		}

		let mut probes = [ReflectionProbeData::default(); MAX_REFLECTION_PROBES as usize];

		for probe in probes.iter_mut().take(header.probe_count as usize) {
			let Some(value) = read_pod(&bytes, &mut at) else {
				log_error!(
					"Could not read the reflection probes from {}",
					path.display()
				);
				return false;
			};

			*probe = value;
		}

		let per_probe = REFLECTION_HALFS_PER_PROBE as usize;
		let mut texels = Vec::with_capacity(header.probe_count as usize * per_probe);

		for _ in 0..header.probe_count as usize * per_probe {
			let Some(half) = read_pod::<u16>(&bytes, &mut at) else {
				log_error!(
					"Could not read the reflection probes from {}",
					path.display()
				);
				return false;
			};

			texels.push(half);
		}

		self.reflection_probes = probes;
		self.reflection_texels = texels;
		self.reflection_count = header.probe_count;
		self.reflections_baked = true;

		for probe in 0..self.reflection_count {
			self.upload_reflection_cubemap(probe);
		}

		self.upload_reflection_probes(self.reflection_count);

		log_info!(
			"Loaded {} reflection probes from {}",
			self.reflection_count,
			path.display()
		);

		true
	}
}

#[allow(dead_code)]
const _: usize = GRID_BYTES_STRIDE;

#[cfg(test)]
mod tests {
	use super::*;

	#[derive(Default)]
	struct Recorded {
		volume_writes: u32,
		probe_writes: Vec<(u64, usize)>,
		rows: Vec<u32>,
	}

	struct FakeGpu(std::sync::Arc<std::sync::Mutex<Recorded>>);

	impl ProbeGpu for FakeGpu {
		fn wait_idle(&mut self) {}

		fn write_volumes(&mut self, _: &[u8]) {
			self.0.lock().unwrap().volume_writes += 1;
		}

		fn write_grid(&mut self, _: &[u8]) {}

		fn write_probes(&mut self, offset: u64, bytes: &[u8]) {
			self.0
				.lock()
				.unwrap()
				.probe_writes
				.push((offset, bytes.len()));
		}

		fn write_moments_row(&mut self, row: u32, _: &[u16]) {
			self.0.lock().unwrap().rows.push(row);
		}

		fn write_reflection_probes(&mut self, _: &[u8]) {}

		fn write_reflection_cubemap(&mut self, _: u32, _: &[u16]) {}

		fn create_capture_resources(&mut self) {}

		fn copy_capture_to_staging(&mut self, _: CaptureKind, _: u32, _: u32) {}

		fn read_capture_color(&mut self, _: CaptureKind, _: u32, _: u32) -> Option<Vec<u16>> {
			Some(vec![0x3C00; (CAPTURE_SIZE * CAPTURE_SIZE * 4) as usize])
		}

		fn read_capture_depth(&mut self, _: u32, _: u32) -> Option<Vec<f32>> {
			Some(vec![0.5; (CAPTURE_SIZE * CAPTURE_SIZE) as usize])
		}
	}

	struct EmptyScene;

	impl ProbeScene for EmptyScene {
		fn placement_boxes(&self) -> PlacementBoxes {
			let mut boxes = PlacementBoxes::default();
			boxes.extend_bounds(Vec3f::new(0.0, 0.0, 0.0), Vec3f::new(8.0, 4.0, 8.0));
			boxes
		}

		fn volume_brushes(&self) -> Vec<VolumeBrush> {
			Vec::new()
		}

		fn reflection_boxes(&self) -> Vec<ReflectionBox> {
			Vec::new()
		}

		fn probe_file_path(&self) -> PathBuf {
			std::env::temp_dir().join("raptor_probes_test.fxprobe")
		}
	}

	fn make_manager() -> (ProbeManager, std::sync::Arc<std::sync::Mutex<Recorded>>) {
		let recorded = std::sync::Arc::new(std::sync::Mutex::new(Recorded::default()));

		(
			ProbeManager::new(Box::new(FakeGpu(recorded.clone()))),
			recorded,
		)
	}

	#[test]
	fn a_new_manager_has_the_default_volume_uploaded() {
		let (manager, recorded) = make_manager();

		assert_eq!(manager.volume_count(), 1);
		assert_eq!(manager.probe_count(), 32);
		assert!(recorded.lock().unwrap().volume_writes > 0);
		assert_eq!(manager.volume_of_probe(5), 0);
		assert_eq!(manager.volume_of_probe(500), MAX_VOLUMES);
	}

	#[test]
	fn a_bake_captures_every_probe_in_batches_and_finishes_idle() {
		let (mut manager, _) = make_manager();

		manager.begin_bake();

		let mut faces = 0;

		while manager.is_baking() {
			manager.record_capture_batch(&mut |_, _| faces += 1);
			manager.service_capture_bake();
		}

		assert_eq!(faces, 6 * manager.probe_count() * manager.bounce_count);
	}

	#[test]
	fn probes_round_trip_through_their_file() {
		let (mut manager, _) = make_manager();

		assert!(manager.save_probes(&EmptyScene));

		let (mut other, _) = make_manager();

		other.clear_volumes();

		assert!(other.load_probes(&EmptyScene));
		assert_eq!(other.probe_count(), manager.probe_count());

		let _ = std::fs::remove_file(EmptyScene.probe_file_path());
	}

	#[test]
	fn a_grid_too_small_is_rejected() {
		let (mut manager, _) = make_manager();

		assert!(!manager.add_volume(
			&EmptyScene,
			Vec3f::ZERO,
			Vec3f::splat(4.0),
			ProbeGridSize { x: 1, y: 4, z: 4 }
		));
	}
}
