use raptor_anim::{NO_BONE, Skeleton};
use raptor_core::log_warn;
use raptor_gfx::Gfx;

/// Advances the skeleton's animation once for the frame, however many objects or passes share it,
/// and puts its pose where the skinned vertices read it
pub fn update_skeleton(gfx: &Gfx, skeleton: &mut Skeleton, delta_time: f32) {
	let frame = gfx.elapsed_frame_count();

	if !skeleton.update(frame, delta_time) {
		return;
	}

	let matrices = skeleton.skinning_matrices();
	let count = matrices.len() as u32;

	let bytes: Vec<u8> = matrices
		.iter()
		.flat_map(|matrix| matrix.0.iter().flatten())
		.flat_map(|value| value.to_ne_bytes())
		.collect();

	let base = gfx
		.buffers()
		.bone
		.lock()
		.unwrap_or_else(|poisoned| poisoned.into_inner())
		.copy_slots(gfx.frame_number(), &bytes, count);

	if base.is_none() {
		log_warn!(Render; "Bone buffer is full, {count} bones will not be drawn");
	}

	skeleton.set_bone_buffer_base(base.unwrap_or(NO_BONE));
}
