use std::ffi::c_void;

use raptor_entity::{EntityCore, ObjectCore};
use raptor_math::{Frustum, Mat4f};
use raptor_render::render_lists::ListBuilder;
use raptor_render::render_lists::ListSinks;
use raptor_world::object_store::ObjectStore;

use crate::world_grid::RxWorldGrid;

pub type RxObjectStore = ObjectStore;

const NONE: u32 = u32::MAX;

#[unsafe(no_mangle)]
pub extern "C" fn rx_object_store_new(capacity: u32) -> *mut RxObjectStore
{
	Box::into_raw(Box::new(ObjectStore::new(capacity)))
}

/// # Safety
///
/// `store` must be null or come from `rx_object_store_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_free(store: *mut RxObjectStore)
{
	if !store.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(store) });
	}
}

/// Takes a slot for a new object and gives back its id, or `u32::MAX` if there is no room. The
/// records last until the object is freed.
///
/// # Safety
///
/// `store` must be live and the outputs writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_alloc(
	store: *const RxObjectStore,
	name_hash: u32,
	out_entity: *mut *mut EntityCore,
	out_object: *mut *mut ObjectCore,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	let Some(handles) = unsafe { &*store }.alloc(name_hash) else {
		return NONE;
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		out_entity.write(handles.entity);
		out_object.write(handles.object);
	}

	handles.id
}

/// # Safety
///
/// `store` must be live and the outputs writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_get(
	store: *const RxObjectStore,
	id: u32,
	out_entity: *mut *mut EntityCore,
	out_object: *mut *mut ObjectCore,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	let Some(handles) = unsafe { &*store }.get(id) else {
		return 0;
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		out_entity.write(handles.entity);
		out_object.write(handles.object);
	}

	1
}

/// # Safety
///
/// `store` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_is_used(store: *const RxObjectStore, id: u32) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*store }.is_used(id))
}

/// # Safety
///
/// `store` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_release(store: *const RxObjectStore, id: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*store }.free(id);
}

/// # Safety
///
/// `store` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_set_name_hash(
	store: *const RxObjectStore,
	id: u32,
	hash: u32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*store }.set_name_hash(id, hash);
}

/// # Safety
///
/// `store` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_find_by_name(store: *const RxObjectStore, hash: u32)
-> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*store }.find_by_name_hash(hash).unwrap_or(NONE)
}

/// Writes the ids of the objects in use, lowest first, returning how many there are. At most
/// `capacity` are written.
///
/// # Safety
///
/// `store` must be live and `out` writable for `capacity` ids.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_used_ids(
	store: *const RxObjectStore,
	out: *mut u32,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let ids = unsafe { &*store }.used_ids();

	for (index, id) in ids.iter().take(capacity).enumerate() {
		// SAFETY: `index` is below `capacity`.
		unsafe { out.add(index).write(*id) };
	}

	ids.len()
}

/// Writes the ids of the objects that have any of `tags`, returning how many there are.
///
/// # Safety
///
/// As for `rx_object_store_used_ids`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_ids_with_tags(
	store: *const RxObjectStore,
	tags: u32,
	out: *mut u32,
	capacity: usize,
) -> usize
{
	// SAFETY: guaranteed by the caller.
	let ids = unsafe { &*store }.ids_with_tags(tags);

	for (index, id) in ids.iter().take(capacity).enumerate() {
		// SAFETY: `index` is below `capacity`.
		unsafe { out.add(index).write(*id) };
	}

	ids.len()
}

/// # Safety
///
/// `store` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_len(store: *const RxObjectStore) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*store }.len()
}

/// # Safety
///
/// `store` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_store_clear(store: *const RxObjectStore)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*store }.clear();
}

pub type RxListBuilder = ListBuilder<'static>;

#[repr(C)]
pub struct RxCullStats
{
	pub tested: u32,
	pub culled: u32,
}

/// Starts the render lists of a frame. `view_projection`, sixteen floats, is the matrix of the
/// camera the frustum is made from, or null to draw everything that is in a tile.
///
/// # Safety
///
/// `store` must outlive the builder.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_list_builder_new(
	store: *const RxObjectStore,
	view_projection: *const f32,
	shadow_pipeline: u32,
) -> *mut RxListBuilder
{
	// SAFETY: guaranteed by the caller.
	let (store, frustum) = unsafe {
		(
			&*store,
			(!view_projection.is_null()).then(|| {
				Frustum::from_view_projection(&Mat4f::from_rows(
					&*view_projection.cast::<[f32; 16]>(),
				))
			}),
		)
	};

	Box::into_raw(Box::new(ListBuilder::new(store, frustum, shadow_pipeline)))
}

/// # Safety
///
/// `builder` must be null or come from `rx_list_builder_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_list_builder_free(builder: *mut RxListBuilder)
{
	if !builder.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(builder) });
	}
}

/// Adds the objects of a tile. `pipeline_for_material` says which pipeline draws a material and
/// `add` takes an object into a pipeline's list. `user` is passed to both.
///
/// # Safety
///
/// `builder` and `grid` must be live and the callbacks callable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_list_builder_add_tile(
	builder: *mut RxListBuilder,
	grid: *const RxWorldGrid,
	tile: u32,
	user: *mut c_void,
	pipeline_for_material: unsafe extern "C" fn(user: *mut c_void, material: u32) -> u32,
	add: unsafe extern "C" fn(user: *mut c_void, pipeline: u32, id: u32),
)
{
	// SAFETY: guaranteed by the caller.
	let (builder, grid) = unsafe { (&mut *builder, &*grid) };

	let Some(objects) = grid.tile_objects(tile) else {
		return;
	};

	let objects = objects.to_vec();

	// SAFETY: guaranteed by the caller.
	let mut material_pipeline = |material: u32| unsafe { pipeline_for_material(user, material) };
	let mut put = |pipeline: u32, id: u32| unsafe { add(user, pipeline, id) };

	builder.add_tile(
		&objects,
		&mut ListSinks {
			pipeline_for_material: &mut material_pipeline,
			add: &mut put,
		},
	);
}

/// # Safety
///
/// `builder` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_list_builder_stats(builder: *const RxListBuilder, out: *mut RxCullStats)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let stats = (*builder).stats;

		out.write(RxCullStats {
			tested: stats.tested,
			culled: stats.culled,
		});
	}
}
