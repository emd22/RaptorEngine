use std::panic::{AssertUnwindSafe, catch_unwind};

use raptor_render::probe_capture::{self, CaptureTable, FACES, MOMENT_TEXELS, Mat4, SH_COEFFS};

const _: () = {
	assert!(SH_COEFFS == 9);
	assert!(MOMENT_TEXELS * 2 == 3072);
	assert!(probe_capture::DEPTH_SIZE == 16);
	assert!(probe_capture::DEPTH_MAX_DISTANCE == 50.0);
};

pub struct RxProbeCapture(CaptureTable);

/// # Safety
///
/// `out` must be valid for writes of `size * size * 2` `u16`s.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_dfg_lut(size: u32, out: *mut u16) -> i32
{
	let Some(len) = (size as usize)
		.checked_mul(size as usize)
		.and_then(|texels| texels.checked_mul(2))
	else {
		return 0;
	};

	if out.is_null() {
		return 0;
	}

	// SAFETY: guaranteed by the caller.
	let out = unsafe { std::slice::from_raw_parts_mut(out, len) };

	i32::from(
		catch_unwind(AssertUnwindSafe(|| {
			raptor_render::dfg::generate_lut(size, out)
		}))
		.unwrap_or(false),
	)
}

/// # Safety
///
/// `inv_projection` must point to 16 floats and `face_inv_view_projection` to `6 * 16` floats, all
/// row-major.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_capture_new(
	size: u32,
	inv_projection: *const f32,
	face_inv_view_projection: *const f32,
) -> *mut RxProbeCapture
{
	// SAFETY: guaranteed by the caller.
	let (inv_projection, faces) = unsafe {
		(
			*inv_projection.cast::<Mat4>(),
			*face_inv_view_projection.cast::<[Mat4; FACES]>(),
		)
	};

	catch_unwind(move || {
		CaptureTable::new(size as usize, inv_projection, &faces)
			.map_or(std::ptr::null_mut(), |table| {
				Box::into_raw(Box::new(RxProbeCapture(table)))
			})
	})
	.unwrap_or(std::ptr::null_mut())
}

/// # Safety
///
/// `capture` must come from `rx_probe_capture_new`. `colors` and `depths` must each point to 6
/// pointers, to `size * size * 4` half floats and `size * size` floats respectively. `out_sh` must
/// be valid for 36 floats and `out_moments` for 3072.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_capture_project(
	capture: *const RxProbeCapture,
	colors: *const *const u16,
	depths: *const *const f32,
	out_sh: *mut f32,
	out_moments: *mut f32,
) -> i32
{
	// SAFETY: guaranteed by the caller.
	let capture = unsafe { &*capture };
	let pixels = capture.0.size() * capture.0.size();

	// SAFETY: guaranteed by the caller.
	let (colors, depths) = unsafe {
		(
			std::slice::from_raw_parts(colors, FACES),
			std::slice::from_raw_parts(depths, FACES),
		)
	};

	let colors: [&[u16]; FACES] = std::array::from_fn(|face| {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(colors[face], pixels * 4) }
	});
	let depths: [&[f32]; FACES] = std::array::from_fn(|face| {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(depths[face], pixels) }
	});

	let Some(projection) = catch_unwind(AssertUnwindSafe(|| capture.0.project(colors, depths)))
		.ok()
		.flatten()
	else {
		return 0;
	};

	// SAFETY: guaranteed by the caller.
	unsafe {
		std::ptr::copy_nonoverlapping(projection.sh.as_ptr().cast::<f32>(), out_sh, SH_COEFFS * 4);
		std::ptr::copy_nonoverlapping(projection.moments.as_ptr(), out_moments, MOMENT_TEXELS * 2);
	}

	1
}

/// # Safety
///
/// `capture` must be null or come from `rx_probe_capture_new`, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_capture_free(capture: *mut RxProbeCapture)
{
	if !capture.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(capture) });
	}
}

/// # Safety
///
/// `sky` and `ground` must point to 3 floats, and `out_sh` must be valid for 36 floats.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_probe_sky_gradient(
	sky: *const f32,
	ground: *const f32,
	out_sh: *mut f32,
)
{
	// SAFETY: guaranteed by the caller.
	let (sky, ground) = unsafe { (*sky.cast::<[f32; 3]>(), *ground.cast::<[f32; 3]>()) };
	let sh = probe_capture::sky_gradient_sh(sky, ground);

	// SAFETY: guaranteed by the caller.
	unsafe { std::ptr::copy_nonoverlapping(sh.as_ptr().cast::<f32>(), out_sh, SH_COEFFS * 4) };
}
