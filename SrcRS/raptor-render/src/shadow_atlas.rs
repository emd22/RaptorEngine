pub const WIDTH: u32 = 4096;
pub const HEIGHT: u32 = 2048;

pub const DIRECTIONAL_SIZE: u32 = 2048;
pub const SPOT_TILE_SIZE: u32 = 512;

pub const SPOT_TILES_PER_ROW: u32 = (WIDTH - DIRECTIONAL_SIZE) / SPOT_TILE_SIZE;
pub const MAX_SPOT_TILES: u32 = SPOT_TILES_PER_ROW * (HEIGHT / SPOT_TILE_SIZE);

const _: () = assert!(
	DIRECTIONAL_SIZE <= HEIGHT,
	"The directional region must fit in the atlas"
);
const _: () = assert!(
	MAX_SPOT_TILES <= 32,
	"Spot tiles are tracked with a 32 bit mask"
);

/// A rectangle of the shadow atlas, in texels
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Region
{
	pub offset_x: u32,
	pub offset_y: u32,
	pub width: u32,
	pub height: u32,
}

/// The atlas is laid out with the directional light on the left, with the largest chunk of the
/// resolution, and the spot light tiles on the right, each 512 by 512:
///
/// ```text
/// +-------------------+----+----+----+----+
/// |                   |  0 |  1 |  2 |  3 |
/// |                   +----+----+----+----+
/// |                   |  4 |  5 |  6 |  7 |
/// |       Sun         +----+----+----+----+
/// |                   |  8 |  9 | 10 | 11 |
/// |                   +----+----+----+----+
/// |                   | 12 | 13 | 14 | 15 |
/// +-------------------+----+----+----+----+
/// ```
pub fn directional_region() -> Region
{
	Region {
		offset_x: 0,
		offset_y: 0,
		width: DIRECTIONAL_SIZE,
		height: DIRECTIONAL_SIZE,
	}
}

pub fn spot_tile_region(tile: u32) -> Option<Region>
{
	if tile >= MAX_SPOT_TILES {
		return None;
	}

	let column = tile % SPOT_TILES_PER_ROW;
	let row = tile / SPOT_TILES_PER_ROW;

	Some(Region {
		offset_x: DIRECTIONAL_SIZE + column * SPOT_TILE_SIZE,
		offset_y: row * SPOT_TILE_SIZE,
		width: SPOT_TILE_SIZE,
		height: SPOT_TILE_SIZE,
	})
}

/// The scale (xy) and offset (zw) that take a UV inside of `region` to a UV in the whole atlas.
pub fn region_uv_transform(region: Region) -> [f32; 4]
{
	let width = WIDTH as f32;
	let height = HEIGHT as f32;

	[
		region.width as f32 / width,
		region.height as f32 / height,
		region.offset_x as f32 / width,
		region.offset_y as f32 / height,
	]
}

/// Which parts of the atlas are in use, and whether what is baked into it can still be trusted.
pub struct ShadowAtlas
{
	spot_tiles_in_use: u32,
	generation: u32,
	needs_clear: bool,
	initialized: bool,
}

impl Default for ShadowAtlas
{
	fn default() -> Self
	{
		Self {
			spot_tiles_in_use: 0,
			generation: 0,
			needs_clear: true,
			initialized: false,
		}
	}
}

impl ShadowAtlas
{
	/// Takes the first free spot light tile, or none if every tile is in use.
	pub fn allocate_spot_tile(&mut self) -> Option<u32>
	{
		let tile = (0..MAX_SPOT_TILES).find(|tile| self.spot_tiles_in_use & (1 << tile) == 0)?;

		self.spot_tiles_in_use |= 1 << tile;

		Some(tile)
	}

	pub fn free_spot_tile(&mut self, tile: u32)
	{
		if tile < MAX_SPOT_TILES {
			self.spot_tiles_in_use &= !(1 << tile);
		}
	}

	/// Throws away everything baked into the atlas, which is cleared by the next region that
	/// begins. This makes every spot light bake again.
	pub fn invalidate(&mut self)
	{
		self.generation = self.generation.wrapping_add(1);
		self.needs_clear = true;
	}

	/// Changes whenever the atlas contents are lost. Bakes from an older generation are no longer
	/// in it.
	pub fn generation(&self) -> u32
	{
		self.generation
	}

	/// Whether the atlas has been rendered to, and is ready to be sampled.
	pub fn is_initialized(&self) -> bool
	{
		self.initialized
	}

	/// Starts a pass over `region` and returns the area its render pass clears. The pass clears
	/// only the region, so the baked spot light tiles survive between frames, except for the
	/// first pass after the atlas was invalidated, which clears all of it since the image
	/// starts out undefined.
	pub fn begin_region(&mut self, region: Region) -> Region
	{
		self.initialized = true;

		if std::mem::take(&mut self.needs_clear) {
			return Region {
				offset_x: 0,
				offset_y: 0,
				width: WIDTH,
				height: HEIGHT,
			};
		}

		region
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn the_layout_has_sixteen_spot_tiles_beside_the_sun()
	{
		assert_eq!(SPOT_TILES_PER_ROW, 4);
		assert_eq!(MAX_SPOT_TILES, 16);

		assert_eq!(
			spot_tile_region(0).unwrap(),
			Region {
				offset_x: 2048,
				offset_y: 0,
				width: 512,
				height: 512
			}
		);
		assert_eq!(spot_tile_region(5).unwrap().offset_x, 2048 + 512);
		assert_eq!(spot_tile_region(5).unwrap().offset_y, 512);
		assert_eq!(spot_tile_region(15).unwrap().offset_x, 2048 + 3 * 512);
		assert_eq!(spot_tile_region(15).unwrap().offset_y, 3 * 512);
		assert!(spot_tile_region(16).is_none());
	}

	#[test]
	fn tiles_never_overlap_each_other_or_the_sun()
	{
		let mut regions = vec![directional_region()];

		regions.extend((0..MAX_SPOT_TILES).map(|tile| spot_tile_region(tile).unwrap()));

		for (index, a) in regions.iter().enumerate() {
			assert!(a.offset_x + a.width <= WIDTH && a.offset_y + a.height <= HEIGHT);

			for b in &regions[index + 1..] {
				let apart = a.offset_x + a.width <= b.offset_x
					|| b.offset_x + b.width <= a.offset_x
					|| a.offset_y + a.height <= b.offset_y
					|| b.offset_y + b.height <= a.offset_y;

				assert!(apart, "{a:?} overlaps {b:?}");
			}
		}
	}

	#[test]
	fn the_uv_transform_takes_a_region_to_its_place_in_the_atlas()
	{
		assert_eq!(
			region_uv_transform(directional_region()),
			[0.5, 1.0, 0.0, 0.0]
		);

		let transform = region_uv_transform(spot_tile_region(1).unwrap());

		assert_eq!(transform, [0.125, 0.25, (2048.0 + 512.0) / 4096.0, 0.0]);
	}

	#[test]
	fn tiles_are_given_out_lowest_first_and_can_be_returned()
	{
		let mut atlas = ShadowAtlas::default();

		let tiles: Vec<_> = (0..MAX_SPOT_TILES)
			.map(|_| atlas.allocate_spot_tile().unwrap())
			.collect();

		assert_eq!(tiles, (0..MAX_SPOT_TILES).collect::<Vec<_>>());
		assert_eq!(atlas.allocate_spot_tile(), None);

		atlas.free_spot_tile(7);
		atlas.free_spot_tile(99);

		assert_eq!(atlas.allocate_spot_tile(), Some(7));
		assert_eq!(atlas.allocate_spot_tile(), None);
	}

	#[test]
	fn only_the_first_region_after_an_invalidation_clears_the_whole_atlas()
	{
		let mut atlas = ShadowAtlas::default();
		let region = spot_tile_region(3).unwrap();

		assert!(!atlas.is_initialized());

		let whole = atlas.begin_region(region);

		assert_eq!((whole.width, whole.height), (WIDTH, HEIGHT));
		assert!(atlas.is_initialized());

		assert_eq!(atlas.begin_region(region), region);

		let generation = atlas.generation();
		atlas.invalidate();

		assert_ne!(atlas.generation(), generation);
		assert_eq!(atlas.begin_region(region).width, WIDTH);
		assert_eq!(atlas.begin_region(region), region);
	}
}
