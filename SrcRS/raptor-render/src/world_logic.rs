use raptor_math::bounds::Aabb;
use raptor_math::frustum::frustum_bounding_box;
use raptor_math::{Frustum, Mat4f, Vec3f};
use raptor_world::WorldGrid;

/// The order to draw blended objects in, furthest from the camera first. `centers` holds three
/// floats per object. Objects the same distance away keep their order.
pub fn far_to_near_order(centers: &[f32], camera: [f32; 3]) -> Vec<u32>
{
	let distances: Vec<f32> = centers
		.as_chunks::<3>()
		.0
		.iter()
		.map(|center| {
			let delta = [
				center[0] - camera[0],
				center[1] - camera[1],
				center[2] - camera[2],
			];

			delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]
		})
		.collect();

	let mut order: Vec<u32> = (0..distances.len() as u32).collect();

	order.sort_by(|a, b| distances[*b as usize].total_cmp(&distances[*a as usize]));

	order
}

/// The tiles of a grid a box over the ground can touch, as inclusive corners clamped to the grid.
pub fn clamp_tile_range(min: [u32; 2], max: [u32; 2], grid: [u32; 2]) -> ([u32; 2], [u32; 2])
{
	let clamp = |tile: [u32; 2]| {
		[
			tile[0].min(grid[0].saturating_sub(1)),
			tile[1].min(grid[1].saturating_sub(1)),
		]
	};

	(clamp(min), clamp(max))
}

/// The tiles of the grid that could be in view of a camera, in the order they are drawn in: row by
/// row, over the tiles the box around the view covers.
pub fn visible_tiles(grid: &WorldGrid, view_projection: &Mat4f) -> Vec<u32>
{
	let frustum = Frustum::from_view_projection(view_projection);
	let bounds = frustum_bounding_box(view_projection);

	let (min, max) = (bounds.min.to_array(), bounds.max.to_array());

	let min_tile = grid.tile_to_xy(grid.world_to_tile([min[0], 0.0, min[2]]));
	let max_tile = grid.tile_to_xy(grid.world_to_tile([max[0], 0.0, max[2]]));

	let (min_tile, max_tile) = clamp_tile_range(min_tile, max_tile, grid.grid_size());

	let mut visible = Vec::new();

	for y in min_tile[1]..=max_tile[1] {
		for x in min_tile[0]..=max_tile[0] {
			let tile = grid.tile_from_xy([x, y]);
			let bounds = grid.tile_aabb(tile);

			let tile_box = Aabb::new(Vec3f::from_array(bounds.min), Vec3f::from_array(bounds.max));

			if frustum.tile_intersects_aabb(&tile_box) {
				visible.push(tile);
			}
		}
	}

	visible
}

/// What of an object that casts a spot light's shadow changes how the shadow looks.
pub struct CasterState
{
	pub id: u32,
	pub mesh: u64,
	pub drawable: bool,
	pub world_matrix: [f32; 16],
	pub pose: Option<(u32, bool)>,
}

struct Hasher(u32);

impl Hasher
{
	fn bytes(&mut self, bytes: &[u8])
	{
		for byte in bytes {
			self.0 = (self.0 ^ u32::from(*byte)).wrapping_mul(0x0100_0193);
		}
	}

	fn word(&mut self, word: u32)
	{
		self.bytes(&word.to_ne_bytes());
	}

	fn floats(&mut self, values: &[f32])
	{
		for value in values {
			self.bytes(&value.to_ne_bytes());
		}
	}
}

/// A hash of everything a spot light's baked shadow depends on, which changes when the baked shadow
/// would look different: the atlas being redone, the light moving, or a caster being moved, loaded
/// or posed.
pub fn spot_bake_hash(
	atlas_generation: u32,
	tile: u32,
	shadow_matrix: &[f32; 16],
	casters: &[CasterState],
) -> u32
{
	let mut hasher = Hasher(0x811C_9DC5);

	hasher.word(atlas_generation);
	hasher.word(tile);
	hasher.floats(shadow_matrix);

	for caster in casters {
		hasher.word(caster.id);
		hasher.bytes(&caster.mesh.to_ne_bytes());
		hasher.word(u32::from(caster.drawable));
		hasher.floats(&caster.world_matrix);

		if let Some((pose_hash, has_bones)) = caster.pose {
			hasher.word(pose_hash);
			hasher.word(u32::from(has_bones));
		}
	}

	hasher.0
}

/// Whether a skinned object, whose pose is a sphere in its own space, reaches a sphere of a spot
/// light, with some padding around the mesh.
pub fn skinned_caster_reaches_sphere(
	world_matrix: &[f32; 16],
	scale: f32,
	pose_center: [f32; 3],
	pose_radius: f32,
	padding: f32,
	center: [f32; 3],
	radius: f32,
) -> bool
{
	let m = world_matrix;
	let c = pose_center;

	let world_center = [
		c[0] * m[0] + c[1] * m[4] + c[2] * m[8] + m[12],
		c[0] * m[1] + c[1] * m[5] + c[2] * m[9] + m[13],
		c[0] * m[2] + c[1] * m[6] + c[2] * m[10] + m[14],
	];

	let delta = [
		world_center[0] - center[0],
		world_center[1] - center[1],
		world_center[2] - center[2],
	];

	let distance = (delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]).sqrt();

	distance <= (pose_radius + padding) * scale + radius
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn the_furthest_object_is_drawn_first_and_ties_keep_their_order()
	{
		let centers = [1.0, 0.0, 0.0, 5.0, 0.0, 0.0, -1.0, 0.0, 0.0, 3.0, 0.0, 0.0];

		assert_eq!(far_to_near_order(&centers, [0.0; 3]), vec![1, 3, 0, 2]);
	}

	#[test]
	fn the_camera_position_is_where_distance_is_measured_from()
	{
		let centers = [0.0, 0.0, 0.0, 10.0, 0.0, 0.0];

		assert_eq!(far_to_near_order(&centers, [10.0, 0.0, 0.0]), vec![0, 1]);
	}

	#[test]
	fn tile_ranges_are_kept_inside_the_grid()
	{
		assert_eq!(clamp_tile_range([0, 2], [50, 9], [10, 4]), ([0, 2], [9, 3]));
		assert_eq!(clamp_tile_range([0, 0], [1, 1], [0, 0]), ([0, 0], [0, 0]));
	}

	#[test]
	fn the_bake_hash_changes_with_anything_the_shadow_depends_on()
	{
		let matrix = [1.0; 16];
		let caster = |id, drawable| CasterState {
			id,
			mesh: 5,
			drawable,
			world_matrix: [0.0; 16],
			pose: None,
		};

		let base = spot_bake_hash(1, 2, &matrix, &[caster(3, false)]);

		assert_eq!(base, spot_bake_hash(1, 2, &matrix, &[caster(3, false)]));
		assert_ne!(base, spot_bake_hash(2, 2, &matrix, &[caster(3, false)]));
		assert_ne!(base, spot_bake_hash(1, 3, &matrix, &[caster(3, false)]));
		assert_ne!(base, spot_bake_hash(1, 2, &[0.0; 16], &[caster(3, false)]));
		assert_ne!(base, spot_bake_hash(1, 2, &matrix, &[caster(3, true)]));
		assert_ne!(base, spot_bake_hash(1, 2, &matrix, &[caster(4, false)]));
		assert_ne!(base, spot_bake_hash(1, 2, &matrix, &[]));

		let posed = |hash| CasterState {
			pose: Some((hash, true)),
			..caster(3, false)
		};

		assert_ne!(
			spot_bake_hash(1, 2, &matrix, &[posed(1)]),
			spot_bake_hash(1, 2, &matrix, &[posed(2)])
		);
	}

	#[test]
	fn a_skinned_caster_reaches_a_light_sphere_by_its_padded_pose_sphere()
	{
		let mut identity = [0.0; 16];
		identity[0] = 1.0;
		identity[5] = 1.0;
		identity[10] = 1.0;
		identity[15] = 1.0;

		assert!(skinned_caster_reaches_sphere(
			&identity,
			1.0,
			[0.0; 3],
			1.0,
			0.5,
			[3.0, 0.0, 0.0],
			2.0
		));
		assert!(!skinned_caster_reaches_sphere(
			&identity,
			1.0,
			[0.0; 3],
			1.0,
			0.5,
			[4.0, 0.0, 0.0],
			2.0
		));

		identity[12] = 10.0;

		assert!(skinned_caster_reaches_sphere(
			&identity,
			2.0,
			[0.0; 3],
			1.0,
			0.0,
			[12.0, 0.0, 0.0],
			0.1
		));
	}

	#[test]
	fn only_the_tiles_a_camera_can_see_are_visible()
	{
		let mut grid = WorldGrid::new(Box::new(|_| {}));
		grid.create([8, 8]);

		let view = Mat4f::look_at(
			Vec3f::new(0.0, 5.0, -20.0),
			Vec3f::new(0.0, 0.0, 0.0),
			Vec3f::UP,
		);
		let projection = Mat4f::perspective(1.0, 1.0, 100.0, 0.1);

		let visible = visible_tiles(&grid, &(view * projection));

		assert!(!visible.is_empty());
		assert!(visible.len() < grid.tile_count() as usize);
		assert!(visible.iter().all(|tile| *tile < grid.tile_count()));
	}
}
