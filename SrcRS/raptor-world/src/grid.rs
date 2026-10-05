use std::collections::HashMap;

pub const NULL_TILE: u32 = u32::MAX;
pub const GLOBAL_TILE: u32 = u32::MAX - 1;

/// The value of a slot that holds no item
pub const EMPTY: u32 = u32::MAX;

pub const MAX_OBJECTS_PER_TILE: usize = 64;
pub const MAX_GLOBAL_OBJECTS: usize = 64;
pub const MAX_LIGHTS_PER_TILE: usize = 64;
pub const MAX_GLOBAL_LIGHTS: usize = 64;
pub const MAX_LIGHT_TILE_COUNT: u32 = 64;

const OBJECT_ID_MASK: u32 = 0x00FF_FFFF;
const LIGHT_ID_MASK: u32 = !(1 << 31);

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Aabb
{
	pub min: [f32; 3],
	pub max: [f32; 3],
}

/// Fixed room for a number of ids. New ids take the lowest free slot, so what is already in the other
/// slots stays where it is. Nothing is ever moved, so a pointer to a slot stays good for as long as
/// the slot keeps its id.
#[derive(Debug)]
struct Slots
{
	items: Box<[u32]>,
}

impl Slots
{
	fn new(capacity: usize) -> Self
	{
		Self {
			items: vec![EMPTY; capacity].into_boxed_slice(),
		}
	}

	fn insert(&mut self, id: u32) -> bool
	{
		match self.items.iter_mut().find(|slot| **slot == EMPTY) {
			Some(slot) => {
				*slot = id;
				true
			}
			None => false,
		}
	}

	fn remove(&mut self, id: u32, mask: u32) -> bool
	{
		match self
			.items
			.iter_mut()
			.find(|slot| **slot != EMPTY && **slot & mask == id & mask)
		{
			Some(slot) => {
				*slot = EMPTY;
				true
			}
			None => false,
		}
	}
}

#[derive(Debug, Default)]
struct Tile
{
	objects: Option<Slots>,
	lights: Option<Slots>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Placement
{
	start: u32,
	span: [u32; 2],
}

const GLOBAL_PLACEMENT: Placement = Placement {
	start: GLOBAL_TILE,
	span: [1, 1],
};

/// What an update of an object's tiles did
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ObjectUpdate
{
	/// The object is not in the grid
	NotPlaced,
	/// The object stayed in the tiles it was in
	Unchanged,
	/// The object was put in different tiles, and what is attached to it should follow
	Moved,
	/// The object was put in different tiles by a change that leaves what is attached to it where it is
	Placed,
}

/// Breaks the world into 2D tiles so that finding the objects and lights near a place does not have to
/// look at all of them.
pub struct WorldGrid
{
	grid_size: [u32; 2],
	tile_size: [f32; 2],
	position_offset: [f32; 3],

	tiles: Vec<Tile>,
	global: Tile,

	objects: HashMap<u32, Placement>,
	lights: HashMap<u32, Placement>,

	view_tile: u32,
	nearby: Vec<u32>,
	nearby_valid: bool,

	warn: Box<dyn Fn(&str)>,
}

fn clamp_span(start: u32, end: u32, limit: u32) -> u32
{
	end.wrapping_sub(start).wrapping_add(1).clamp(1, limit.max(1))
}

impl Default for WorldGrid
{
	fn default() -> Self
	{
		Self::new(Box::new(|_| {}))
	}
}

impl WorldGrid
{
	/// `warn` is told about tiles that are full
	pub fn new(warn: Box<dyn Fn(&str)>) -> Self
	{
		Self {
			grid_size: [0, 0],
			tile_size: [10.0, 10.0],
			position_offset: [0.0; 3],
			tiles: Vec::new(),
			global: Tile::default(),
			objects: HashMap::new(),
			lights: HashMap::new(),
			view_tile: NULL_TILE,
			nearby: Vec::new(),
			nearby_valid: false,
			warn,
		}
	}

	/// Makes a grid of `grid_size` tiles, centred on the origin
	pub fn create(&mut self, grid_size: [u32; 2])
	{
		self.grid_size = grid_size;
		self.tiles = (0..u64::from(grid_size[0]) * u64::from(grid_size[1]))
			.map(|_| Tile::default())
			.collect();

		let half = [grid_size[0] / 2, grid_size[1] / 2];

		self.position_offset = [
			half[0] as f32 * self.tile_size[0],
			0.0,
			half[1] as f32 * self.tile_size[1],
		];

		self.nearby_valid = false;
	}

	pub fn grid_size(&self) -> [u32; 2]
	{
		self.grid_size
	}

	pub fn tile_size(&self) -> [f32; 2]
	{
		self.tile_size
	}

	pub fn position_offset(&self) -> [f32; 3]
	{
		self.position_offset
	}

	pub fn tile_count(&self) -> u32
	{
		self.tiles.len() as u32
	}

	pub fn tile_to_xy(&self, tile: u32) -> [u32; 2]
	{
		let width = self.grid_size[0].max(1);
		let x = tile % width;

		[x, (tile - x) / width]
	}

	pub fn tile_from_xy(&self, xy: [u32; 2]) -> u32
	{
		xy[1].min(self.grid_size[1].saturating_sub(1)) * self.grid_size[0]
			+ xy[0].min(self.grid_size[0].saturating_sub(1))
	}

	pub fn world_to_tile(&self, position: [f32; 3]) -> u32
	{
		if self.tiles.is_empty() {
			return 0;
		}

		let x = ((position[0] + self.position_offset[0]) / self.tile_size[0]).floor() as i32;
		let z = ((position[2] + self.position_offset[2]) / self.tile_size[1]).floor() as i32;

		let x = x.clamp(0, self.grid_size[0] as i32 - 1) as u32;
		let z = z.clamp(0, self.grid_size[1] as i32 - 1) as u32;

		z * self.grid_size[0] + x
	}

	pub fn tile_world_center(&self, xy: [u32; 2]) -> [f32; 3]
	{
		[
			(xy[0] as f32 * self.tile_size[0]) - self.position_offset[0] + (self.tile_size[0] * 0.5),
			self.position_offset[1],
			(xy[1] as f32 * self.tile_size[1]) - self.position_offset[2] + (self.tile_size[1] * 0.5),
		]
	}

	pub fn tile_aabb(&self, tile: u32) -> Aabb
	{
		let center = self.tile_world_center(self.tile_to_xy(tile));
		let half = [self.tile_size[0] * 0.5, 1.0, self.tile_size[1] * 0.5];

		Aabb {
			min: [center[0] - half[0], center[1] - half[1], center[2] - half[2]],
			max: [center[0] + half[0], center[1] + half[1], center[2] + half[2]],
		}
	}

	fn tile(&self, index: u32) -> Option<&Tile>
	{
		if index == GLOBAL_TILE {
			Some(&self.global)
		}
		else {
			self.tiles.get(index as usize)
		}
	}

	fn tile_mut(&mut self, index: u32) -> Option<&mut Tile>
	{
		if index == GLOBAL_TILE {
			Some(&mut self.global)
		}
		else {
			self.tiles.get_mut(index as usize)
		}
	}

	/// The slots of the objects in a tile, with `EMPTY` in the free ones. None if there is no such tile
	/// or nothing has been put in it yet.
	pub fn tile_objects(&self, index: u32) -> Option<&[u32]>
	{
		self.tile(index)?.objects.as_ref().map(|slots| &slots.items[..])
	}

	/// The slots of the lights in a tile, with `EMPTY` in the free ones
	pub fn tile_lights(&self, index: u32) -> Option<&[u32]>
	{
		self.tile(index)?.lights.as_ref().map(|slots| &slots.items[..])
	}

	pub fn tile_exists(&self, index: u32) -> bool
	{
		self.tile(index).is_some()
	}

	/// The rectangle of tiles `min` to `max` reaches, as the first tile and how many tiles across and
	/// down.
	fn rect(&self, min: [f32; 3], max: [f32; 3]) -> Placement
	{
		let start = self.tile_to_xy(self.world_to_tile(min));
		let end = self.tile_to_xy(self.world_to_tile(max));

		Placement {
			start: self.tile_from_xy(start),
			span: [
				clamp_span(start[0], end[0], self.grid_size[0]),
				clamp_span(start[1], end[1], self.grid_size[1]),
			],
		}
	}

	fn rect_tiles(&self, placement: Placement) -> Vec<u32>
	{
		let start = self.tile_to_xy(placement.start);

		let mut tiles = Vec::with_capacity((placement.span[0] * placement.span[1]) as usize);

		for y in 0..placement.span[1] {
			for x in 0..placement.span[0] {
				tiles.push(self.tile_from_xy([start[0].wrapping_add(x), start[1].wrapping_add(y)]));
			}
		}

		tiles
	}

	fn insert_object(&mut self, tile_index: u32, id: u32)
	{
		self.nearby_valid = false;

		let capacity = if tile_index == GLOBAL_TILE {
			MAX_GLOBAL_OBJECTS
		}
		else {
			MAX_OBJECTS_PER_TILE
		};

		let Some(tile) = self.tile_mut(tile_index) else {
			return;
		};

		let inserted = tile
			.objects
			.get_or_insert_with(|| Slots::new(capacity))
			.insert(id);

		if !inserted {
			let message = if tile_index == GLOBAL_TILE {
				format!("Global tile is full, cannot insert object {id}")
			}
			else {
				format!("Tile {tile_index} is full, cannot insert object {id}")
			};

			(self.warn)(&message);
		}
	}

	fn remove_object_from(&mut self, tile_index: u32, id: u32)
	{
		if let Some(slots) = self
			.tile_mut(tile_index)
			.and_then(|tile| tile.objects.as_mut())
		{
			slots.remove(id, OBJECT_ID_MASK);
		}
	}

	fn insert_object_rect(&mut self, id: u32, placement: Placement)
	{
		for tile in self.rect_tiles(placement) {
			self.insert_object(tile, id);
		}
	}

	fn remove_object_rect(&mut self, id: u32, placement: Placement)
	{
		for tile in self.rect_tiles(placement) {
			self.remove_object_from(tile, id);
		}
	}

	/// Puts an object in the tiles `bounds` (its bounds in the world) reaches, or in the global tile if
	/// it is not `cullable`.
	pub fn add_object(&mut self, id: u32, bounds: Aabb, cullable: bool)
	{
		if self.objects.contains_key(&id) {
			self.remove_object(id);
		}

		if !cullable {
			self.insert_object(GLOBAL_TILE, id);
			self.objects.insert(id, GLOBAL_PLACEMENT);
			return;
		}

		let placement = self.rect(bounds.min, bounds.max);

		self.insert_object_rect(id, placement);
		self.objects.insert(id, placement);
	}

	/// Moves an object to the tiles it now reaches, if they changed
	pub fn update_object(&mut self, id: u32, bounds: Aabb, cullable: bool) -> ObjectUpdate
	{
		let Some(&current) = self.objects.get(&id) else {
			return ObjectUpdate::NotPlaced;
		};

		self.nearby_valid = false;

		if !cullable {
			if current.start == GLOBAL_TILE {
				return ObjectUpdate::Unchanged;
			}

			self.remove_object_rect(id, current);
			self.insert_object(GLOBAL_TILE, id);
			self.objects.insert(id, GLOBAL_PLACEMENT);

			return ObjectUpdate::Placed;
		}

		let placement = self.rect(bounds.min, bounds.max);

		if current.start == GLOBAL_TILE {
			self.remove_object_from(GLOBAL_TILE, id);
			self.insert_object_rect(id, placement);
			self.objects.insert(id, placement);

			return ObjectUpdate::Placed;
		}

		if placement == current {
			return ObjectUpdate::Unchanged;
		}

		self.remove_object_rect(id, current);
		self.insert_object_rect(id, placement);
		self.objects.insert(id, placement);

		ObjectUpdate::Moved
	}

	pub fn remove_object(&mut self, id: u32)
	{
		self.nearby_valid = false;

		let Some(placement) = self.objects.remove(&id) else {
			return;
		};

		if placement.start == GLOBAL_TILE {
			self.remove_object_from(GLOBAL_TILE, id);
		}
		else {
			self.remove_object_rect(id, placement);
		}
	}

	/// The first tile an object is in, `GLOBAL_TILE` for the global tile, or `NULL_TILE` if it is not
	/// in the grid
	pub fn object_tile(&self, id: u32) -> u32
	{
		self.objects.get(&id).map_or(NULL_TILE, |placement| placement.start)
	}

	/// Where a light goes: the tiles its bounds reach, or the global tile if it is not `cullable`, the
	/// grid is empty, or the light covers more tiles than `MAX_LIGHT_TILE_COUNT`.
	fn light_placement(&self, bounds: Aabb, cullable: bool) -> Placement
	{
		if !cullable || self.tiles.is_empty() {
			return GLOBAL_PLACEMENT;
		}

		let placement = self.rect(bounds.min, bounds.max);

		if placement.span[0] * placement.span[1] > MAX_LIGHT_TILE_COUNT {
			return GLOBAL_PLACEMENT;
		}

		placement
	}

	fn insert_light(&mut self, tile_index: u32, id: u32) -> bool
	{
		let capacity = if tile_index == GLOBAL_TILE {
			MAX_GLOBAL_LIGHTS
		}
		else {
			MAX_LIGHTS_PER_TILE
		};

		let Some(tile) = self.tile_mut(tile_index) else {
			return false;
		};

		let inserted = tile
			.lights
			.get_or_insert_with(|| Slots::new(capacity))
			.insert(id);

		if !inserted {
			let message = if tile_index == GLOBAL_TILE {
				format!("Global tile is full, cannot insert light {id}")
			}
			else {
				format!("Tile {tile_index} is full, cannot insert light {id}")
			};

			(self.warn)(&message);
		}

		inserted
	}

	fn remove_light_from(&mut self, tile_index: u32, id: u32)
	{
		if let Some(slots) = self
			.tile_mut(tile_index)
			.and_then(|tile| tile.lights.as_mut())
		{
			slots.remove(id, LIGHT_ID_MASK);
		}
	}

	pub fn add_light(&mut self, id: u32, bounds: Aabb, cullable: bool)
	{
		if self.lights.contains_key(&id) {
			self.remove_light(id);
		}

		let placement = self.light_placement(bounds, cullable);

		if placement.start == GLOBAL_TILE {
			if self.insert_light(GLOBAL_TILE, id) {
				self.lights.insert(id, GLOBAL_PLACEMENT);
			}

			return;
		}

		for tile in self.rect_tiles(placement) {
			self.insert_light(tile, id);
		}

		self.lights.insert(id, placement);
	}

	pub fn update_light(&mut self, id: u32, bounds: Aabb, cullable: bool)
	{
		let Some(&current) = self.lights.get(&id) else {
			return;
		};

		if self.light_placement(bounds, cullable) == current {
			return;
		}

		self.add_light(id, bounds, cullable);
	}

	pub fn remove_light(&mut self, id: u32)
	{
		let Some(placement) = self.lights.remove(&id) else {
			return;
		};

		if placement.start == GLOBAL_TILE {
			self.remove_light_from(GLOBAL_TILE, id);
			return;
		}

		for tile in self.rect_tiles(placement) {
			self.remove_light_from(tile, id);
		}
	}

	/// The first tile a light is in, `GLOBAL_TILE` for the global tile, or `NULL_TILE` if it is not
	/// in the grid
	pub fn light_tile(&self, id: u32) -> u32
	{
		self.lights.get(&id).map_or(NULL_TILE, |placement| placement.start)
	}

	pub fn set_view_tile(&mut self, tile: u32)
	{
		if self.view_tile != tile {
			self.nearby_valid = false;
		}

		self.view_tile = tile;
	}

	pub fn view_tile(&self) -> u32
	{
		self.view_tile
	}

	/// The objects in the tile of the view and the eight around it, each once
	pub fn nearby_objects(&mut self) -> &[u32]
	{
		if self.nearby_valid {
			return &self.nearby;
		}

		self.nearby.clear();

		if self.view_tile == NULL_TILE || self.tiles.is_empty() {
			return &self.nearby;
		}

		const AROUND: [(i64, i64); 9] = [
			(0, 0),
			(1, 0),
			(-1, 0),
			(0, -1),
			(0, 1),
			(1, 1),
			(1, -1),
			(-1, -1),
			(-1, 1),
		];

		let view = self.tile_to_xy(self.view_tile);

		let mut found = Vec::new();

		for (dx, dy) in AROUND {
			let x = i64::from(view[0]) + dx;
			let y = i64::from(view[1]) + dy;

			if x < 0 || y < 0 || x >= i64::from(self.grid_size[0]) || y >= i64::from(self.grid_size[1]) {
				continue;
			}

			if let Some(slots) = self.tile_objects(self.tile_from_xy([x as u32, y as u32])) {
				found.extend(slots.iter().copied().filter(|id| *id != EMPTY));
			}
		}

		for id in found {
			if !self.nearby.contains(&id) {
				self.nearby.push(id);
			}
		}

		self.nearby_valid = true;

		&self.nearby
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn grid() -> WorldGrid
	{
		let mut grid = WorldGrid::default();
		grid.create([20, 20]);
		grid
	}

	fn bounds(min: [f32; 3], max: [f32; 3]) -> Aabb
	{
		Aabb { min, max }
	}

	fn around(x: f32, z: f32) -> Aabb
	{
		bounds([x - 1.0, 0.0, z - 1.0], [x + 1.0, 1.0, z + 1.0])
	}

	fn ids(slots: Option<&[u32]>) -> Vec<u32>
	{
		slots
			.unwrap_or(&[])
			.iter()
			.copied()
			.filter(|id| *id != EMPTY)
			.collect()
	}

	#[test]
	fn the_grid_is_centred_on_the_origin()
	{
		let grid = grid();

		assert_eq!(grid.position_offset(), [100.0, 0.0, 100.0]);
		assert_eq!(grid.world_to_tile([0.0, 0.0, 0.0]), 10 * 20 + 10);
		assert_eq!(grid.world_to_tile([-100.0, 0.0, -100.0]), 0);
		assert_eq!(grid.world_to_tile([99.0, 0.0, 99.0]), 399);
	}

	#[test]
	fn places_past_the_edge_use_the_edge_tile()
	{
		let grid = grid();

		assert_eq!(grid.world_to_tile([-500.0, 0.0, -500.0]), 0);
		assert_eq!(grid.world_to_tile([500.0, 0.0, 500.0]), 399);
		assert_eq!(grid.world_to_tile([f32::NAN, 0.0, 0.0]) % 20, 0);
	}

	#[test]
	fn tile_numbers_and_coordinates_convert_both_ways()
	{
		let grid = grid();

		for tile in [0, 1, 19, 20, 215, 399] {
			assert_eq!(grid.tile_from_xy(grid.tile_to_xy(tile)), tile);
		}

		assert_eq!(grid.tile_from_xy([99, 3]), 3 * 20 + 19);
		assert_eq!(grid.tile_from_xy([2, 99]), 19 * 20 + 2);
	}

	#[test]
	fn a_tile_centre_is_half_a_tile_from_its_corner()
	{
		let grid = grid();

		assert_eq!(grid.tile_world_center([10, 10]), [5.0, 0.0, 5.0]);
		assert_eq!(grid.tile_world_center([0, 0]), [-95.0, 0.0, -95.0]);

		let aabb = grid.tile_aabb(grid.tile_from_xy([10, 10]));

		assert_eq!(aabb.min, [0.0, -1.0, 0.0]);
		assert_eq!(aabb.max, [10.0, 1.0, 10.0]);
	}

	#[test]
	fn a_small_object_goes_in_one_tile()
	{
		let mut grid = grid();

		grid.add_object(7, around(5.0, 5.0), true);

		let tile = grid.world_to_tile([5.0, 0.0, 5.0]);

		assert_eq!(grid.object_tile(7), tile);
		assert_eq!(ids(grid.tile_objects(tile)), vec![7]);
		assert_eq!(ids(grid.tile_objects(tile + 1)), Vec::<u32>::new());
	}

	#[test]
	fn an_object_across_tile_borders_goes_in_each_of_them()
	{
		let mut grid = grid();

		grid.add_object(7, bounds([-1.0, 0.0, -1.0], [11.0, 1.0, 1.0]), true);

		let start = grid.world_to_tile([-1.0, 0.0, -1.0]);

		assert_eq!(grid.object_tile(7), start);

		for offset in [0, 1, 2, 20, 21, 22] {
			let in_it = ids(grid.tile_objects(start + offset)).contains(&7);

			assert_eq!(in_it, matches!(offset, 0 | 1 | 2 | 20 | 21 | 22), "{offset}");
		}
	}

	#[test]
	fn an_object_that_cannot_be_culled_is_in_the_global_tile()
	{
		let mut grid = grid();

		grid.add_object(3, around(5.0, 5.0), false);

		assert_eq!(grid.object_tile(3), GLOBAL_TILE);
		assert_eq!(ids(grid.tile_objects(GLOBAL_TILE)), vec![3]);
	}

	#[test]
	fn new_ids_take_the_lowest_free_slot()
	{
		let mut grid = grid();

		for id in 1..=3 {
			grid.add_object(id, around(5.0, 5.0), true);
		}

		let tile = grid.world_to_tile([5.0, 0.0, 5.0]);

		grid.remove_object(2);
		grid.add_object(9, around(5.0, 5.0), true);

		assert_eq!(&grid.tile_objects(tile).unwrap()[..3], &[1, 9, 3]);
	}

	#[test]
	fn a_full_tile_warns_and_leaves_the_object_out()
	{
		use std::cell::RefCell;
		use std::rc::Rc;

		let warnings = Rc::new(RefCell::new(Vec::new()));
		let seen = Rc::clone(&warnings);

		let mut grid = WorldGrid::new(Box::new(move |message| seen.borrow_mut().push(message.to_owned())));
		grid.create([20, 20]);

		for id in 0..=MAX_OBJECTS_PER_TILE as u32 {
			grid.add_object(id, around(5.0, 5.0), true);
		}

		let tile = grid.world_to_tile([5.0, 0.0, 5.0]);

		assert_eq!(ids(grid.tile_objects(tile)).len(), MAX_OBJECTS_PER_TILE);
		assert_eq!(warnings.borrow().len(), 1);
	}

	#[test]
	fn moving_an_object_within_its_tile_changes_nothing()
	{
		let mut grid = grid();

		grid.add_object(7, around(5.0, 5.0), true);

		assert_eq!(
			grid.update_object(7, around(6.0, 6.0), true),
			ObjectUpdate::Unchanged
		);
	}

	#[test]
	fn moving_an_object_to_other_tiles_moves_it()
	{
		let mut grid = grid();

		grid.add_object(7, around(5.0, 5.0), true);

		let old_tile = grid.object_tile(7);

		assert_eq!(grid.update_object(7, around(35.0, 5.0), true), ObjectUpdate::Moved);

		assert_ne!(grid.object_tile(7), old_tile);
		assert!(ids(grid.tile_objects(old_tile)).is_empty());
		assert_eq!(ids(grid.tile_objects(grid.object_tile(7))), vec![7]);
	}

	#[test]
	fn an_object_that_stops_being_cullable_moves_to_the_global_tile()
	{
		let mut grid = grid();

		grid.add_object(7, around(5.0, 5.0), true);
		let old_tile = grid.object_tile(7);

		assert_eq!(grid.update_object(7, around(5.0, 5.0), false), ObjectUpdate::Placed);
		assert_eq!(grid.object_tile(7), GLOBAL_TILE);
		assert!(ids(grid.tile_objects(old_tile)).is_empty());

		assert_eq!(grid.update_object(7, around(5.0, 5.0), false), ObjectUpdate::Unchanged);

		assert_eq!(grid.update_object(7, around(5.0, 5.0), true), ObjectUpdate::Placed);
		assert_eq!(grid.object_tile(7), old_tile);
		assert!(ids(grid.tile_objects(GLOBAL_TILE)).is_empty());
	}

	#[test]
	fn an_object_that_is_not_in_the_grid_is_not_updated()
	{
		let mut grid = grid();

		assert_eq!(grid.update_object(7, around(5.0, 5.0), true), ObjectUpdate::NotPlaced);
		assert_eq!(grid.object_tile(7), NULL_TILE);
	}

	#[test]
	fn removing_an_object_takes_it_out_of_every_tile_and_forgets_it()
	{
		let mut grid = grid();

		grid.add_object(7, bounds([-1.0, 0.0, -1.0], [11.0, 1.0, 11.0]), true);
		grid.remove_object(7);

		for tile in 0..grid.tile_count() {
			assert!(ids(grid.tile_objects(tile)).is_empty());
		}

		assert_eq!(grid.object_tile(7), NULL_TILE);
		assert_eq!(grid.update_object(7, around(5.0, 5.0), true), ObjectUpdate::NotPlaced);
	}

	#[test]
	fn adding_an_object_twice_does_not_leave_it_behind_in_the_old_tiles()
	{
		let mut grid = grid();

		grid.add_object(7, around(5.0, 5.0), true);
		let first = grid.object_tile(7);

		grid.add_object(7, around(45.0, 5.0), true);

		assert!(ids(grid.tile_objects(first)).is_empty());
		assert_eq!(ids(grid.tile_objects(grid.object_tile(7))), vec![7]);
	}

	#[test]
	fn a_light_goes_in_every_tile_its_bounds_reach()
	{
		let mut grid = grid();

		grid.add_light(4, bounds([-1.0, 0.0, -1.0], [11.0, 1.0, 1.0]), true);

		let start = grid.world_to_tile([-1.0, 0.0, -1.0]);

		assert_eq!(grid.light_tile(4), start);

		for offset in [0, 1, 2, 20, 21, 22] {
			assert_eq!(ids(grid.tile_lights(start + offset)).contains(&4), matches!(offset, 0 | 1 | 2 | 20 | 21 | 22));
		}
	}

	#[test]
	fn lights_that_cannot_be_culled_or_cover_too_much_are_global()
	{
		let mut grid = grid();

		grid.add_light(1, around(0.0, 0.0), false);
		grid.add_light(2, bounds([-100.0, 0.0, -100.0], [100.0, 1.0, 100.0]), true);

		assert_eq!(grid.light_tile(1), GLOBAL_TILE);
		assert_eq!(grid.light_tile(2), GLOBAL_TILE);
		assert_eq!(ids(grid.tile_lights(GLOBAL_TILE)), vec![1, 2]);
	}

	#[test]
	fn a_light_before_the_grid_exists_is_global()
	{
		let mut grid = WorldGrid::default();

		grid.add_light(1, around(0.0, 0.0), true);

		assert_eq!(grid.light_tile(1), GLOBAL_TILE);
	}

	#[test]
	fn a_light_moves_only_when_its_tiles_change()
	{
		let mut grid = grid();

		grid.add_light(4, around(5.0, 5.0), true);
		let first = grid.light_tile(4);

		grid.update_light(4, around(6.0, 6.0), true);
		assert_eq!(grid.light_tile(4), first);

		grid.update_light(4, around(55.0, 5.0), true);
		assert_ne!(grid.light_tile(4), first);
		assert!(ids(grid.tile_lights(first)).is_empty());

		grid.update_light(4, around(55.0, 5.0), false);
		assert_eq!(grid.light_tile(4), GLOBAL_TILE);
	}

	#[test]
	fn removing_a_light_takes_it_out_of_its_tiles()
	{
		let mut grid = grid();

		grid.add_light(4, bounds([-1.0, 0.0, -1.0], [11.0, 1.0, 11.0]), true);
		grid.remove_light(4);
		grid.add_light(5, around(0.0, 0.0), false);
		grid.remove_light(5);

		for tile in 0..grid.tile_count() {
			assert!(ids(grid.tile_lights(tile)).is_empty());
		}

		assert!(ids(grid.tile_lights(GLOBAL_TILE)).is_empty());
		assert_eq!(grid.light_tile(4), NULL_TILE);
	}

	#[test]
	fn nearby_objects_come_from_the_view_tile_and_the_eight_around_it_once_each()
	{
		let mut grid = grid();

		grid.add_object(1, around(5.0, 5.0), true);
		grid.add_object(2, around(15.0, 5.0), true);
		grid.add_object(3, around(-15.0, -5.0), true);
		grid.add_object(4, around(55.0, 5.0), true);
		grid.add_object(5, bounds([-1.0, 0.0, -1.0], [11.0, 1.0, 11.0]), true);

		let view = grid.world_to_tile([5.0, 0.0, 5.0]);
		grid.set_view_tile(view);

		let mut nearby = grid.nearby_objects().to_vec();
		nearby.sort_unstable();

		assert_eq!(nearby, vec![1, 2, 5]);
	}

	#[test]
	fn the_view_at_the_edge_does_not_pull_in_the_other_side_of_the_grid()
	{
		let mut grid = grid();

		grid.add_object(1, around(-95.0, -95.0), true);
		grid.add_object(2, around(95.0, -95.0), true);
		grid.add_object(3, around(-95.0, 95.0), true);

		grid.set_view_tile(0);

		assert_eq!(grid.nearby_objects().to_vec(), vec![1]);
	}

	#[test]
	fn there_are_no_nearby_objects_until_a_view_tile_is_set()
	{
		let mut grid = grid();

		grid.add_object(1, around(5.0, 5.0), true);

		assert!(grid.nearby_objects().is_empty());
	}

	#[test]
	fn nearby_objects_follow_changes_to_the_grid()
	{
		let mut grid = grid();

		let view = grid.world_to_tile([5.0, 0.0, 5.0]);
		grid.set_view_tile(view);

		assert!(grid.nearby_objects().is_empty());

		grid.add_object(1, around(5.0, 5.0), true);
		assert_eq!(grid.nearby_objects().to_vec(), vec![1]);

		grid.update_object(1, around(95.0, 95.0), true);
		assert!(grid.nearby_objects().is_empty());
	}

	#[test]
	fn the_slot_of_an_object_stays_where_it_is_while_others_come_and_go()
	{
		let mut grid = grid();

		grid.add_object(1, around(5.0, 5.0), true);

		let tile = grid.world_to_tile([5.0, 0.0, 5.0]);
		let address = grid.tile_objects(tile).unwrap().as_ptr();

		for id in 2..10 {
			grid.add_object(id, around(5.0, 5.0), true);
		}

		grid.remove_object(5);

		assert_eq!(grid.tile_objects(tile).unwrap().as_ptr(), address);
	}
}
