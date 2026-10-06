use raptor_math::frustum::ALL_PLANES;
use raptor_math::{Frustum, Vec3f};
use raptor_world::random;

pub const MAX_DECALS: usize = 512;
pub const MAX_VISIBLE_DECALS: usize = 512;

pub const BULLET_HOLE_ATLAS_PATH: &str = "Textures/bulletholes.png";
pub const BULLET_HOLE_NORMAL_ATLAS_PATH: &str = "Textures/bulletholes_normal.png";
pub const BLOOD_ATLAS_PATH: &str = "Textures/blood_splat.png";

const BULLET_HOLE_ATLAS_COLUMNS: u32 = 4;
const BULLET_HOLE_ATLAS_ROWS: u32 = 4;
const BULLET_HOLE_SIZE: f32 = 0.20;
const BULLET_HOLE_DEPTH: f32 = 0.1;
const BULLET_HOLE_NORMAL_STRENGTH: f32 = 1.0;
const BLOOD_DEPTH: f32 = 0.2;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DecalAtlas {
	BulletHoles = 0,
	Blood = 1,
}

#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct DecalGpuData {
	pub world_to_decal: [f32; 16],
	pub center: [f32; 3],
	pub color: u32,
	pub axis_x: [f32; 3],
	pub roughness: f32,
	pub axis_y: [f32; 3],
	pub roughness_weight: f32,
	pub axis_z: [f32; 3],
	pub normal_strength: f32,
	pub atlas_rect: [f32; 4],
	pub half_extents: [f32; 4],
}

const _: () = assert!(size_of::<DecalGpuData>() == 160);

// SAFETY: `repr(C)` with only 4 byte numeric fields, laid out with no padding (the size assert
// above is the sum of the fields).
unsafe impl bytemuck::Zeroable for DecalGpuData {}
// SAFETY: as above, and every bit pattern is a valid value of each field.
unsafe impl bytemuck::Pod for DecalGpuData {}

#[derive(Clone, Copy, Debug)]
pub struct DecalDesc {
	pub position: Vec3f,
	pub direction: Vec3f,
	pub width: f32,
	pub height: f32,
	pub depth: f32,
	pub roll: f32,
	pub atlas_rect: [f32; 4],
	pub color: u32,
	pub roughness: f32,
	pub roughness_weight: f32,
	pub normal_strength: f32,
	pub atlas: DecalAtlas,
}

impl Default for DecalDesc {
	fn default() -> Self {
		Self {
			position: Vec3f::ZERO,
			direction: Vec3f::FORWARD,
			width: 0.25,
			height: 0.25,
			depth: 0.1,
			roll: 0.0,
			atlas_rect: [1.0, 1.0, 0.0, 0.0],
			color: 0xFFFF_FFFF,
			roughness: 0.5,
			roughness_weight: 0.4,
			normal_strength: 0.6,
			atlas: DecalAtlas::BulletHoles,
		}
	}
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct AtlasStatus {
	pub atlas: bool,
	pub normal_atlas: bool,
	pub blood_atlas: bool,
}

#[derive(Clone, Copy)]
struct DecalEntry {
	gpu: DecalGpuData,
	bounds_radius: f32,
}

pub struct DecalManager {
	decals: Vec<DecalEntry>,
	next_slot: usize,
	visible_slots: Vec<usize>,
	visible_count: usize,
}

impl Default for DecalManager {
	fn default() -> Self {
		Self::new()
	}
}

impl DecalManager {
	pub fn new() -> Self {
		Self {
			decals: Vec::with_capacity(MAX_DECALS),
			next_slot: 0,
			visible_slots: Vec::with_capacity(MAX_DECALS),
			visible_count: 0,
		}
	}

	pub fn visible_count(&self) -> u32 {
		self.visible_count as u32
	}

	pub fn count(&self) -> usize {
		self.decals.len()
	}

	pub fn clear(&mut self) {
		self.decals.clear();
		self.next_slot = 0;
	}

	pub fn add_decal(&mut self, desc: &DecalDesc) {
		let forward = desc.direction.normalize();

		let reference = if forward.y.abs() < 0.99 {
			Vec3f::UP
		} else {
			Vec3f::FORWARD
		};

		let unrolled_right = reference.cross(&forward).normalize();
		let unrolled_up = forward.cross(&unrolled_right);

		let (roll_sin, roll_cos) = desc.roll.sin_cos();

		let right = unrolled_right * roll_cos + unrolled_up * roll_sin;
		let up = unrolled_up * roll_cos - unrolled_right * roll_sin;

		let mut gpu = DecalGpuData::default();

		let scaled_axes = [
			right * (1.0 / desc.width),
			up * (1.0 / desc.height),
			forward * (1.0 / desc.depth),
		];

		for (axis, scaled) in scaled_axes.iter().enumerate() {
			gpu.world_to_decal[axis] = scaled.x;
			gpu.world_to_decal[4 + axis] = scaled.y;
			gpu.world_to_decal[8 + axis] = scaled.z;
			gpu.world_to_decal[12 + axis] = -scaled.dot(&desc.position);
			gpu.world_to_decal[axis * 4 + 3] = 0.0;
		}

		gpu.world_to_decal[15] = 1.0;

		gpu.center = desc.position.to_array();
		gpu.axis_x = right.to_array();
		gpu.axis_y = up.to_array();
		gpu.axis_z = forward.to_array();

		gpu.half_extents = [
			desc.width * 0.5,
			desc.height * 0.5,
			desc.depth * 0.5,
			desc.atlas as u32 as f32,
		];

		gpu.color = desc.color;
		gpu.roughness = desc.roughness;
		gpu.roughness_weight = desc.roughness_weight;
		gpu.normal_strength = desc.normal_strength;
		gpu.atlas_rect = desc.atlas_rect;

		let entry = DecalEntry {
			gpu,
			bounds_radius: 0.5
				* (desc.width * desc.width + desc.height * desc.height + desc.depth * desc.depth)
					.sqrt(),
		};

		if self.decals.len() < MAX_DECALS {
			self.decals.push(entry);
			self.next_slot = self.decals.len() % MAX_DECALS;
			return;
		}

		self.decals[self.next_slot] = entry;
		self.next_slot = (self.next_slot + 1) % MAX_DECALS;
	}

	pub fn add_bullet_hole(&mut self, hit_point: Vec3f, hit_normal: Vec3f) {
		let variants = BULLET_HOLE_ATLAS_COLUMNS * BULLET_HOLE_ATLAS_ROWS;
		let cell_width = 1.0 / BULLET_HOLE_ATLAS_COLUMNS as f32;
		let cell_height = 1.0 / BULLET_HOLE_ATLAS_ROWS as f32;

		let variant = random::fast_rand32() % variants;
		let size = BULLET_HOLE_SIZE * (1.0 + 0.15 * random::unit());

		let column = variant % BULLET_HOLE_ATLAS_COLUMNS;
		let row = variant / BULLET_HOLE_ATLAS_COLUMNS;

		self.add_decal(&DecalDesc {
			position: hit_point,
			direction: -hit_normal,
			width: size,
			height: size,
			depth: BULLET_HOLE_DEPTH,
			roll: random::unit() * std::f32::consts::TAU,
			atlas_rect: [
				cell_width,
				cell_height,
				column as f32 * cell_width,
				row as f32 * cell_height,
			],
			roughness: 0.4,
			roughness_weight: 1.0,
			normal_strength: BULLET_HOLE_NORMAL_STRENGTH,
			..DecalDesc::default()
		});
	}

	pub fn add_blood_splat(&mut self, hit_point: Vec3f, hit_normal: Vec3f, size: f32) {
		let width = size * (0.8 + 0.4 * random::unit());
		let height = width * (0.85 + 0.3 * random::unit());

		let shade = (0.65 + 0.35 * random::unit()) * 255.0;
		let shade = shade as u32;

		self.add_decal(&DecalDesc {
			position: hit_point,
			direction: -hit_normal,
			width,
			height,
			depth: BLOOD_DEPTH,
			roll: random::unit() * std::f32::consts::TAU,
			atlas_rect: [1.0, 1.0, 0.0, 0.0],
			color: shade | (shade << 8) | (shade << 16) | 0xFF00_0000,
			roughness: 0.2,
			roughness_weight: 0.85,
			normal_strength: 0.0,
			atlas: DecalAtlas::Blood,
			..DecalDesc::default()
		});
	}

	pub fn update(&mut self, frustum: &Frustum, status: AtlasStatus, page: &mut [DecalGpuData]) {
		self.visible_slots.clear();
		self.visible_count = 0;

		if !status.atlas {
			return;
		}

		let count = self.decals.len();

		for i in 0..count {
			let slot = (self.next_slot + i) % count;
			let entry = &self.decals[slot];

			if !status.blood_atlas && entry.gpu.half_extents[3] > 0.5 {
				continue;
			}

			if frustum.intersects_sphere(
				Vec3f::from_array(entry.gpu.center),
				entry.bounds_radius,
				ALL_PLANES,
			) {
				self.visible_slots.push(slot);
			}
		}

		let visible = self.visible_slots.len();
		let first = visible.saturating_sub(MAX_VISIBLE_DECALS.min(page.len()));

		for &slot in &self.visible_slots[first..] {
			let mut gpu = self.decals[slot].gpu;

			if !status.normal_atlas {
				gpu.normal_strength = 0.0;
			}

			page[self.visible_count] = gpu;
			self.visible_count += 1;
		}
	}
}

#[cfg(test)]
mod tests {
	use raptor_math::Mat4f;

	use super::*;

	fn everything_in_view() -> Frustum {
		let projection = Mat4f::perspective(1.5, 1.0, 100.0, 0.1);
		let view = Mat4f::look_at(Vec3f::ZERO, Vec3f::FORWARD, Vec3f::UP);

		Frustum::from_view_projection(&(view * projection))
	}

	const ATLASES: AtlasStatus = AtlasStatus {
		atlas: true,
		normal_atlas: true,
		blood_atlas: true,
	};

	#[test]
	fn a_decal_maps_its_center_to_the_middle_of_decal_space() {
		let mut decals = DecalManager::new();

		decals.add_decal(&DecalDesc {
			position: Vec3f::new(1.0, 2.0, 3.0),
			direction: Vec3f::new(0.0, 0.0, 1.0),
			width: 2.0,
			height: 2.0,
			depth: 1.0,
			..DecalDesc::default()
		});

		let m = decals.decals[0].gpu.world_to_decal;
		let (x, y, z) = (1.0, 2.0, 3.0);

		assert!((x * m[0] + y * m[4] + z * m[8] + m[12]).abs() < 1e-5);
		assert!((x * m[1] + y * m[5] + z * m[9] + m[13]).abs() < 1e-5);
		assert!((x * m[2] + y * m[6] + z * m[10] + m[14]).abs() < 1e-5);
	}

	#[test]
	fn the_ring_replaces_the_oldest_decal_when_full() {
		let mut decals = DecalManager::new();

		for _ in 0..MAX_DECALS + 3 {
			decals.add_bullet_hole(Vec3f::new(0.0, 0.0, 5.0), Vec3f::new(0.0, 0.0, -1.0));
		}

		assert_eq!(decals.count(), MAX_DECALS);
		assert_eq!(decals.next_slot, 3);
	}

	#[test]
	fn only_decals_in_view_are_uploaded_and_nothing_before_the_atlas_loads() {
		let mut decals = DecalManager::new();
		let mut page = vec![DecalGpuData::default(); MAX_VISIBLE_DECALS];

		decals.add_bullet_hole(Vec3f::new(0.0, 0.0, 5.0), Vec3f::new(0.0, 0.0, -1.0));
		decals.add_bullet_hole(Vec3f::new(0.0, 0.0, -50.0), Vec3f::new(0.0, 0.0, 1.0));

		decals.update(&everything_in_view(), AtlasStatus::default(), &mut page);
		assert_eq!(decals.visible_count(), 0);

		decals.update(&everything_in_view(), ATLASES, &mut page);
		assert_eq!(decals.visible_count(), 1);
		assert_eq!(page[0].center, [0.0, 0.0, 5.0]);
	}

	#[test]
	fn blood_waits_for_its_atlas_and_a_flat_decal_without_normals() {
		let mut decals = DecalManager::new();
		let mut page = vec![DecalGpuData::default(); MAX_VISIBLE_DECALS];

		decals.add_blood_splat(Vec3f::new(0.0, 0.0, 5.0), Vec3f::new(0.0, 0.0, -1.0), 1.0);
		decals.add_bullet_hole(Vec3f::new(0.0, 0.0, 6.0), Vec3f::new(0.0, 0.0, -1.0));

		let no_blood = AtlasStatus {
			blood_atlas: false,
			normal_atlas: false,
			..ATLASES
		};

		decals.update(&everything_in_view(), no_blood, &mut page);

		assert_eq!(decals.visible_count(), 1);
		assert_eq!(page[0].normal_strength, 0.0);
	}
}
