use raptor_math::Vec3f;
use raptor_math::quat_platform as q;

fn quat(values: *const f32) -> q::QUAT
{
	// SAFETY: callers pass four floats.
	let values = unsafe { std::slice::from_raw_parts(values, 4) };

	q::set(values[0], values[1], values[2], values[3])
}

fn put(out: *mut f32, value: q::QUAT)
{
	// SAFETY: callers pass room for four floats.
	unsafe { std::slice::from_raw_parts_mut(out, 4) }.copy_from_slice(&q::get_values(value));
}

/// # Safety
///
/// `axis` must hold three floats and `out` have room for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_quat_from_axis_angle(axis: *const f32, angle: f32, out: *mut f32)
{
	// SAFETY: guaranteed by the caller.
	let axis = unsafe { std::slice::from_raw_parts(axis, 3) };

	put(
		out,
		q::from_axis_angle(Vec3f::new(axis[0], axis[1], axis[2]).0, angle),
	);
}

/// # Safety
///
/// `angles` must hold three floats and `out` have room for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_quat_from_euler(angles: *const f32, out: *mut f32)
{
	// SAFETY: guaranteed by the caller.
	let angles = unsafe { std::slice::from_raw_parts(angles, 3) };

	put(
		out,
		q::from_euler_angles(Vec3f::new(angles[0], angles[1], angles[2]).0),
	);
}

/// # Safety
///
/// `quat` must hold four floats and `out` have room for three.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_quat_euler_angles(rotation: *const f32, out: *mut f32)
{
	let angles = q::get_euler_angles(quat(rotation));

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts_mut(out, 3) }
		.copy_from_slice(&Vec3f::from_vector(angles).to_array());
}

/// # Safety
///
/// Each quaternion holds four floats, and `out` has room for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_quat_mul(left: *const f32, right: *const f32, out: *mut f32)
{
	put(out, q::mul(quat(left), quat(right)));
}

/// # Safety
///
/// Each quaternion holds four floats, and `out` has room for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_quat_slerp(from: *const f32, to: *const f32, step: f32, out: *mut f32)
{
	put(out, q::slerp(quat(from), quat(to), step));
}

/// # Safety
///
/// Each quaternion holds four floats, and `out` has room for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_quat_nlerp(from: *const f32, to: *const f32, time: f32, out: *mut f32)
{
	put(out, q::nlerp(quat(from), quat(to), time));
}

/// Turns `rotation` by `angle` around the axis, then renormalizes so repeated turns do not
/// accumulate scale.
///
/// # Safety
///
/// `rotation` holds four floats, `axis` three, and `out` has room for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_quat_rotate_by_axis(
	rotation: *const f32,
	axis: *const f32,
	angle: f32,
	out: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let axis = unsafe { std::slice::from_raw_parts(axis, 3) };

	let turn = q::from_axis_angle(Vec3f::new(axis[0], axis[1], axis[2]).0, angle);

	put(out, q::normalize(q::mul(quat(rotation), turn)));
}

/// The rotation that takes forward (+Z) to `direction` by the shortest arc.
///
/// # Safety
///
/// `direction` must hold three floats and `out` have room for four.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_quat_from_direction(direction: *const f32, out: *mut f32)
{
	// SAFETY: guaranteed by the caller.
	let direction = unsafe { std::slice::from_raw_parts(direction, 3) };

	let dir = Vec3f::new(direction[0], direction[1], direction[2]).normalize();

	let w = 1.0 + dir.z;

	if w < 1e-6 {
		put(out, q::set(0.0, 1.0, 0.0, 0.0));
	} else {
		put(out, q::normalize(q::set(-dir.y, dir.x, 0.0, w)));
	}
}
