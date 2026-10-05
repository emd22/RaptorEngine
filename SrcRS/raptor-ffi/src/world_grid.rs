use std::ffi::CString;

use raptor_world::{Aabb, GLOBAL_TILE, NULL_TILE, ObjectUpdate, WorldGrid};

use crate::{LogFn, RxLogSink};

pub type RxWorldGrid = WorldGrid;

pub const OBJECT_NOT_PLACED: u32 = 0;
pub const OBJECT_UNCHANGED: u32 = 1;
pub const OBJECT_MOVED: u32 = 2;
pub const OBJECT_PLACED: u32 = 3;

const LOG_WARNING: i32 = 2;
const LOG_CATEGORY_CORE: i32 = 0;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxTileSlots
{
	pub objects: *const u32,
	pub object_slots: u32,
	pub lights: *const u32,
	pub light_slots: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxTileBounds
{
	pub min: [f32; 3],
	pub max: [f32; 3],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RxGridInfo
{
	pub grid_size: [u32; 2],
	pub tile_size: [f32; 2],
	pub position_offset: [f32; 3],
	pub tile_count: u32,
	pub view_tile: u32,
}

fn bounds_of(min: &[f32; 3], max: &[f32; 3]) -> Aabb
{
	Aabb {
		min: *min,
		max: *max,
	}
}

/// Makes a grid. `log` receives warnings about tiles that are full, and must outlive the grid.
///
/// # Safety
///
/// `log` must be null or valid for as long as the grid lives.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_new(log: *const RxLogSink) -> *mut RxWorldGrid
{
	let sink = (!log.is_null()).then(|| {
		// SAFETY: guaranteed by the caller.
		let sink = unsafe { &*log };

		(sink.user as usize, sink.log)
	});

	let warn: Box<dyn Fn(&str)> = Box::new(move |message| {
		let Some((user, Some(log))) = sink else {
			return;
		};

		let log: LogFn = log;
		let text = CString::new(message.replace('\0', "")).unwrap_or_default();

		// SAFETY: the host promised `log` accepts a pointer and length pair.
		unsafe {
			log(
				user as *mut std::ffi::c_void,
				LOG_WARNING,
				LOG_CATEGORY_CORE,
				text.as_ptr(),
				text.as_bytes().len(),
			);
		}
	});

	Box::into_raw(Box::new(WorldGrid::new(warn)))
}

/// # Safety
///
/// `grid` must be null or come from `rx_world_grid_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_free(grid: *mut RxWorldGrid)
{
	if !grid.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(grid) });
	}
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_create(grid: *mut RxWorldGrid, width: u32, height: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *grid }.create([width, height]);
}

/// # Safety
///
/// `grid` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_info(grid: *const RxWorldGrid, out: *mut RxGridInfo)
{
	// SAFETY: guaranteed by the caller.
	let grid = unsafe { &*grid };

	// SAFETY: guaranteed by the caller.
	unsafe {
		out.write(RxGridInfo {
			grid_size: grid.grid_size(),
			tile_size: grid.tile_size(),
			position_offset: grid.position_offset(),
			tile_count: grid.tile_count(),
			view_tile: grid.view_tile(),
		});
	}
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_world_to_tile(
	grid: *const RxWorldGrid,
	x: f32,
	y: f32,
	z: f32,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*grid }.world_to_tile([x, y, z])
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_tile_to_xy(
	grid: *const RxWorldGrid,
	tile: u32,
	out: *mut u32,
)
{
	// SAFETY: guaranteed by the caller; `out` has room for two values.
	unsafe {
		let xy = (*grid).tile_to_xy(tile);

		out.write(xy[0]);
		out.add(1).write(xy[1]);
	}
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_tile_from_xy(grid: *const RxWorldGrid, x: u32, y: u32) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*grid }.tile_from_xy([x, y])
}

/// # Safety
///
/// `grid` must be live and `out` have room for three values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_tile_world_center(
	grid: *const RxWorldGrid,
	x: u32,
	y: u32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let center = (*grid).tile_world_center([x, y]);

		for (index, value) in center.iter().enumerate() {
			out.add(index).write(*value);
		}
	}
}

/// # Safety
///
/// `grid` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_tile_bounds(
	grid: *const RxWorldGrid,
	tile: u32,
	out: *mut RxTileBounds,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let bounds = (*grid).tile_aabb(tile);

		out.write(RxTileBounds {
			min: bounds.min,
			max: bounds.max,
		});
	}
}

/// What is in a tile. The slots hold `u32::MAX` where there is nothing. The pointers stay good for
/// as long as the tile does, and only the contents of the slots change.
///
/// # Safety
///
/// `grid` must be live and `out` writable. Returns 0, and writes nothing, if there is no such tile.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_tile_slots(
	grid: *const RxWorldGrid,
	tile: u32,
	out: *mut RxTileSlots,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	let grid = unsafe { &*grid };

	if !grid.tile_exists(tile) {
		return 0;
	}

	let (objects, lights) = (grid.tile_objects(tile), grid.tile_lights(tile));

	// SAFETY: guaranteed by the caller.
	unsafe {
		out.write(RxTileSlots {
			objects: objects.map_or(std::ptr::null(), <[u32]>::as_ptr),
			object_slots: objects.map_or(0, |slots| slots.len() as u32),
			lights: lights.map_or(std::ptr::null(), <[u32]>::as_ptr),
			light_slots: lights.map_or(0, |slots| slots.len() as u32),
		});
	}

	1
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_add_object(
	grid: *mut RxWorldGrid,
	id: u32,
	min: *const [f32; 3],
	max: *const [f32; 3],
	cullable: u8,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *grid }.add_object(id, bounds_of(unsafe { &*min }, unsafe { &*max }), cullable != 0);
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_update_object(
	grid: *mut RxWorldGrid,
	id: u32,
	min: *const [f32; 3],
	max: *const [f32; 3],
	cullable: u8,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	let result = unsafe { &mut *grid }.update_object(id, bounds_of(unsafe { &*min }, unsafe { &*max }), cullable != 0);

	match result {
		ObjectUpdate::NotPlaced => OBJECT_NOT_PLACED,
		ObjectUpdate::Unchanged => OBJECT_UNCHANGED,
		ObjectUpdate::Moved => OBJECT_MOVED,
		ObjectUpdate::Placed => OBJECT_PLACED,
	}
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_remove_object(grid: *mut RxWorldGrid, id: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *grid }.remove_object(id);
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_object_tile(grid: *const RxWorldGrid, id: u32) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*grid }.object_tile(id)
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_add_light(
	grid: *mut RxWorldGrid,
	id: u32,
	min: *const [f32; 3],
	max: *const [f32; 3],
	cullable: u8,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *grid }.add_light(id, bounds_of(unsafe { &*min }, unsafe { &*max }), cullable != 0);
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_update_light(
	grid: *mut RxWorldGrid,
	id: u32,
	min: *const [f32; 3],
	max: *const [f32; 3],
	cullable: u8,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *grid }.update_light(id, bounds_of(unsafe { &*min }, unsafe { &*max }), cullable != 0);
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_remove_light(grid: *mut RxWorldGrid, id: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *grid }.remove_light(id);
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_light_tile(grid: *const RxWorldGrid, id: u32) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*grid }.light_tile(id)
}

/// # Safety
///
/// `grid` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_set_view_tile(grid: *mut RxWorldGrid, tile: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *grid }.set_view_tile(tile);
}

/// The ids of the objects near the view. They stay valid until the grid changes.
///
/// # Safety
///
/// `grid` must be live and `count` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_world_grid_nearby_objects(
	grid: *mut RxWorldGrid,
	count: *mut u32,
) -> *const u32
{
	// SAFETY: guaranteed by the caller.
	let nearby = unsafe { &mut *grid }.nearby_objects();

	// SAFETY: guaranteed by the caller.
	unsafe { count.write(nearby.len() as u32) };

	nearby.as_ptr()
}

pub const RX_WORLD_GRID_GLOBAL_TILE: u32 = GLOBAL_TILE;
pub const RX_WORLD_GRID_NULL_TILE: u32 = NULL_TILE;

const _: () = {
	use std::mem::size_of;

	assert!(size_of::<RxTileSlots>() == 32);
	assert!(size_of::<RxTileBounds>() == 24);
	assert!(size_of::<RxGridInfo>() == 36);
};
