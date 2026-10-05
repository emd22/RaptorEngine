use raptor_math::mat4::{self, Quat};
use raptor_render::debug_draw::{self, DebugDraw, DrawCommand, Queued, Shape};

pub type RxDebugDraw = DebugDraw;
pub type RxDebugDrawCommand = DrawCommand;

fn code(queued: Queued) -> i32
{
	match queued {
		Queued::Yes => 0,
		Queued::DroppedFirst => 1,
		Queued::Dropped => 2,
	}
}

unsafe fn vec3(values: *const f32) -> [f32; 3]
{
	// SAFETY: guaranteed by the caller.
	unsafe { [*values, *values.add(1), *values.add(2)] }
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_debug_draw_new() -> *mut RxDebugDraw
{
	Box::into_raw(Box::new(DebugDraw::default()))
}

/// # Safety
///
/// `draw` must be null or come from `rx_debug_draw_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_debug_draw_free(draw: *mut RxDebugDraw)
{
	if !draw.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(draw) });
	}
}

/// Queues a shape at a world matrix. Returns 0 if it was queued, 1 if the queue was full and this
/// is the first shape dropped since it was cleared, and 2 if it was full and one was already
/// dropped.
///
/// # Safety
///
/// `draw` must be live and `matrix` valid for 16 floats, row by row.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_debug_draw_shape(
	draw: *mut RxDebugDraw,
	shape: u32,
	matrix: *const f32,
	color: u32,
) -> i32
{
	let Some(shape) = Shape::from_raw(shape) else {
		return 2;
	};

	// SAFETY: guaranteed by the caller.
	let flat: [f32; 16] = unsafe { *matrix.cast::<[f32; 16]>() };

	// SAFETY: guaranteed by the caller.
	code(unsafe { &mut *draw }.draw(shape, mat4::unflatten(&flat), color))
}

/// # Safety
///
/// `draw` must be live and `from` and `to` valid for 3 floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_debug_draw_line(
	draw: *mut RxDebugDraw,
	from: *const f32,
	to: *const f32,
	color: u32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let (from, to) = unsafe { (vec3(from), vec3(to)) };

	// SAFETY: guaranteed by the caller.
	code(unsafe { &mut *draw }.line(from, to, color))
}

/// Queues a box given by its center, half extent and rotation (x, y, z, w).
///
/// # Safety
///
/// `draw` must be live, `center` and `half_extent` valid for 3 floats and `rotation` for 4.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_debug_draw_box(
	draw: *mut RxDebugDraw,
	shape: u32,
	center: *const f32,
	half_extent: *const f32,
	rotation: *const f32,
	color: u32,
) -> i32
{
	let Some(shape) = Shape::from_raw(shape) else {
		return 2;
	};

	// SAFETY: guaranteed by the caller.
	let (center, half_extent, rotation) = unsafe {
		(
			vec3(center),
			vec3(half_extent),
			Quat {
				x: *rotation,
				y: *rotation.add(1),
				z: *rotation.add(2),
				w: *rotation.add(3),
			},
		)
	};

	// SAFETY: guaranteed by the caller.
	code(unsafe { &mut *draw }.draw(
		shape,
		debug_draw::box_matrix(center, half_extent, rotation),
		color,
	))
}

/// Queues an axis aligned box given by its corners.
///
/// # Safety
///
/// `draw` must be live and `min` and `max` valid for 3 floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_debug_draw_aabb(
	draw: *mut RxDebugDraw,
	shape: u32,
	min: *const f32,
	max: *const f32,
	color: u32,
) -> i32
{
	let Some(shape) = Shape::from_raw(shape) else {
		return 2;
	};

	// SAFETY: guaranteed by the caller.
	let (min, max) = unsafe { (vec3(min), vec3(max)) };

	let (center, half_extent) = debug_draw::aabb_center_and_extent(min, max);

	// SAFETY: guaranteed by the caller.
	code(unsafe { &mut *draw }.draw(
		shape,
		debug_draw::box_matrix(center, half_extent, Quat::IDENTITY),
		color,
	))
}

/// # Safety
///
/// `draw` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_debug_draw_count(draw: *const RxDebugDraw) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*draw }.queued_count() as u32
}

/// Gets the draws for the queued shapes, grouped by kind of shape, with each shape's matrix taken
/// through the camera matrix. They stay valid until the next call or `rx_debug_draw_clear`.
///
/// # Safety
///
/// `draw` must be live, `camera` valid for 16 floats, and the outputs writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_debug_draw_commands(
	draw: *mut RxDebugDraw,
	camera: *const f32,
	out_commands: *mut *const RxDebugDrawCommand,
	out_count: *mut usize,
)
{
	// SAFETY: guaranteed by the caller.
	let flat: [f32; 16] = unsafe { *camera.cast::<[f32; 16]>() };

	// SAFETY: guaranteed by the caller.
	let commands = unsafe { &mut *draw }.commands(&mat4::unflatten(&flat));

	// SAFETY: guaranteed by the caller.
	unsafe {
		*out_commands = commands.as_ptr();
		*out_count = commands.len();
	}
}

/// # Safety
///
/// `draw` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_debug_draw_clear(draw: *mut RxDebugDraw)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *draw }.clear();
}
