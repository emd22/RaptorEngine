use raptor_entity::{EntityCore, ObjectCore};

pub type RxEntityCore = EntityCore;

fn vec3(values: *const f32) -> [f32; 3]
{
	// SAFETY: callers pass three floats.
	let values = unsafe { std::slice::from_raw_parts(values, 3) };

	[values[0], values[1], values[2]]
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_entity_core_new() -> *mut RxEntityCore
{
	Box::into_raw(Box::default())
}

/// # Safety
///
/// `core` must be null or come from `rx_entity_core_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_core_free(core: *mut RxEntityCore)
{
	if !core.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(core) });
	}
}

/// # Safety
///
/// `core` must be live and `position` hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_set_position(core: *mut RxEntityCore, position: *const f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.set_position(vec3(position));
}

/// # Safety
///
/// `core` must be live and `rotation` hold four floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_set_rotation(core: *mut RxEntityCore, rotation: *const f32)
{
	// SAFETY: guaranteed by the caller.
	let (core, rotation) = unsafe { (&mut *core, std::slice::from_raw_parts(rotation, 4)) };

	core.set_rotation([rotation[0], rotation[1], rotation[2], rotation[3]]);
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_set_scale(core: *mut RxEntityCore, scale: f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.set_scale(scale);
}

/// # Safety
///
/// `core` must be live and `origin` hold three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_set_rotation_origin(core: *mut RxEntityCore, origin: *const f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.set_rotation_origin(vec3(origin));
}

/// Writes the rotation after turning by `angle` about an axis of the entity's own space.
///
/// # Safety
///
/// `core` must be live, `axis` hold three floats and `out` have room for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_rotated_by_axis(
	core: *const RxEntityCore,
	axis: *const f32,
	angle: f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts_mut(out, 4) }
		.copy_from_slice(&unsafe { &*core }.rotated_by_axis(vec3(axis), angle));
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_update_matrix(core: *mut RxEntityCore)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.update_matrix_if_out_of_date();
}

/// # Safety
///
/// `core` must be live and `matrix` hold sixteen floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_set_model_matrix(core: *mut RxEntityCore, matrix: *const f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.set_model_matrix(unsafe { &*matrix.cast::<[f32; 16]>() });
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_submit_needed(core: *mut RxEntityCore, frame: u32) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.submit_needed(frame)
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_mark_transform_out_of_date(core: *mut RxEntityCore)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.mark_transform_out_of_date();
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_mark_matrix_out_of_date(core: *mut RxEntityCore)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.mark_matrix_out_of_date();
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_is_matrix_out_of_date(core: *const RxEntityCore) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*core }.is_matrix_out_of_date()
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_is_physics_out_of_date(core: *const RxEntityCore) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*core }.is_physics_out_of_date()
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_entity_set_physics_out_of_date(core: *mut RxEntityCore, value: bool)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.set_physics_out_of_date(value);
}

pub type RxObjectCore = ObjectCore;

#[unsafe(no_mangle)]
pub extern "C" fn rx_object_core_new() -> *mut RxObjectCore
{
	Box::into_raw(Box::default())
}

/// # Safety
///
/// `core` must be null or come from `rx_object_core_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_core_free(core: *mut RxObjectCore)
{
	if !core.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(core) });
	}
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_is_probe_visible(core: *const RxObjectCore) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { &*core }.is_probe_visible())
}

/// The ids of the nodes attached to an object.
///
/// # Safety
///
/// `core` must be live and `out_count` writable. The pointer lasts until the children change.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_children(
	core: *const RxObjectCore,
	out_count: *mut usize,
) -> *const u32
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let children = (*core).children();

		out_count.write(children.len());

		children.as_ptr()
	}
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_add_child(core: *mut RxObjectCore, id: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.add_child(id);
}

/// # Safety
///
/// `core` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_clear_children(core: *mut RxObjectCore)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *core }.clear_children();
}
