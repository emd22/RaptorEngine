use raptor_math::{Aabb, Mat4f, Vec3f};
use raptor_render::object_logic::{
	CullInputs, can_be_frustum_culled, contains_point, direction_scale, merge_child_bounds,
	raycast_bounds,
};

#[repr(C)]
pub struct RxCullInputs
{
	pub cullable: bool,
	pub has_mesh: bool,
	pub world_layer: bool,
	pub skinned: bool,
	pub is_instance: bool,
	pub physics_enabled: bool,
	pub instance_slots_in_use: u32,
}

unsafe fn vec3(values: *const f32) -> Vec3f
{
	// SAFETY: guaranteed by the caller.
	let values = unsafe { std::slice::from_raw_parts(values, 3) };

	Vec3f::new(values[0], values[1], values[2])
}

unsafe fn bounds(min: *const f32, max: *const f32) -> Aabb
{
	// SAFETY: guaranteed by the caller.
	unsafe { Aabb::new(vec3(min), vec3(max)) }
}

unsafe fn matrix(rows: *const f32) -> Mat4f
{
	// SAFETY: guaranteed by the caller.
	Mat4f::from_rows(unsafe { &*rows.cast::<[f32; 16]>() })
}

/// # Safety
///
/// `inputs` must be valid and the bounds hold three floats each.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_can_be_frustum_culled(
	inputs: *const RxCullInputs,
	bounds_min: *const f32,
	bounds_max: *const f32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	let inputs = unsafe { &*inputs };

	let inputs = CullInputs {
		cullable: inputs.cullable,
		has_mesh: inputs.has_mesh,
		world_layer: inputs.world_layer,
		skinned: inputs.skinned,
		instance_slots_in_use: inputs.instance_slots_in_use,
		is_instance: inputs.is_instance,
		physics_enabled: inputs.physics_enabled,
	};

	// SAFETY: guaranteed by the caller.
	can_be_frustum_culled(&inputs, &unsafe { bounds(bounds_min, bounds_max) })
}

/// # Safety
///
/// The bounds and point hold three floats each and the matrix sixteen, in row order.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_contains_point(
	bounds_min: *const f32,
	bounds_max: *const f32,
	world_matrix: *const f32,
	point: *const f32,
) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		contains_point(
			&bounds(bounds_min, bounds_max),
			&matrix(world_matrix),
			vec3(point),
		)
	}
}

/// Returns the distance along the ray to the object's bounds, or -1 on a miss, and writes the face
/// that was met.
///
/// # Safety
///
/// As for `rx_object_contains_point`, with `out_face` writable for three floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_raycast_bounds(
	bounds_min: *const f32,
	bounds_max: *const f32,
	world_matrix: *const f32,
	origin: *const f32,
	direction: *const f32,
	out_face: *mut f32,
) -> f32
{
	// SAFETY: guaranteed by the caller.
	let (distance, face) = unsafe {
		raycast_bounds(
			&bounds(bounds_min, bounds_max),
			&matrix(world_matrix),
			vec3(origin),
			vec3(direction),
		)
	};

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts_mut(out_face, 3) }.copy_from_slice(&face.to_array());

	distance
}

/// # Safety
///
/// The bounds and direction hold three floats each.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_direction_scale(
	bounds_min: *const f32,
	bounds_max: *const f32,
	scale: f32,
	direction: *const f32,
) -> f32
{
	// SAFETY: guaranteed by the caller.
	unsafe { direction_scale(&bounds(bounds_min, bounds_max), scale, vec3(direction)) }
}

/// Takes a child's bounds into its parent's: widens the parent's box (in `parent_min` and
/// `parent_max`, in place) to hold the corners of the child's box.
///
/// # Safety
///
/// The bounds hold three floats each, the parent's being writable, and the matrices sixteen.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_object_merge_child_bounds(
	parent_min: *mut f32,
	parent_max: *mut f32,
	parent_world: *const f32,
	child_min: *const f32,
	child_max: *const f32,
	child_world: *const f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe {
		let merged = merge_child_bounds(
			&bounds(parent_min, parent_max),
			&matrix(parent_world),
			&bounds(child_min, child_max),
			&matrix(child_world),
		);

		std::slice::from_raw_parts_mut(parent_min, 3).copy_from_slice(&merged.min.to_array());
		std::slice::from_raw_parts_mut(parent_max, 3).copy_from_slice(&merged.max.to_array());
	}
}
