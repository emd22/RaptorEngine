use std::ffi::{CStr, c_char};

use raptor_anim::{
	AnimationEnd, AnimationId, MAX_ANIMATION_STACK, Mat4, Playback, RestPose, Skeleton, SkeletonData,
	SkeletonFields,
};

pub type RxSkeleton = SkeletonFields;
pub type RxPlayback = Playback;
pub type RxRestPose = RestPose;

/// # Safety
///
/// `skeleton` must come from this module and be live.
unsafe fn skeleton_mut<'a>(skeleton: *mut RxSkeleton) -> &'a mut Skeleton
{
	// SAFETY: `Skeleton` starts with its `SkeletonFields`, so the pointers are interchangeable.
	unsafe { &mut *skeleton.cast::<Skeleton>() }
}

/// # Safety
///
/// `skeleton` must come from this module and be live.
unsafe fn skeleton_ref<'a>(skeleton: *const RxSkeleton) -> &'a Skeleton
{
	// SAFETY: as in `skeleton_mut`.
	unsafe { &*skeleton.cast::<Skeleton>() }
}

/// # Safety
///
/// `name` must be null or a NUL-terminated string.
unsafe fn string_from(name: *const c_char) -> String
{
	if name.is_null() {
		return String::new();
	}

	// SAFETY: guaranteed by the caller.
	unsafe { CStr::from_ptr(name) }
		.to_string_lossy()
		.into_owned()
}

/// # Safety
///
/// `ptr` must be null or point at `count` floats.
unsafe fn floats<'a>(ptr: *const f32, count: usize) -> &'a [f32]
{
	if ptr.is_null() || count == 0 {
		return &[];
	}

	// SAFETY: guaranteed by the caller.
	unsafe { std::slice::from_raw_parts(ptr, count) }
}

/// Makes a skeleton. `inverse_bind` and `root_transforms` are `joint_count` row-major matrices,
/// `rest_pose` is `joint_count` rest poses, and `names` is `joint_count` strings. Any of those may
/// be null.
///
/// # Safety
///
/// Every pointer must be null or point at `joint_count` items of its kind. The data is copied.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_new(
	joint_count: u32,
	inverse_bind: *const f32,
	parents: *const u32,
	rest_pose: *const RxRestPose,
	root_transforms: *const f32,
	names: *const *const c_char,
) -> *mut RxSkeleton
{
	let count = joint_count as usize;

	let matrices = |ptr: *const f32| {
		(!ptr.is_null()).then(|| {
			// SAFETY: guaranteed by the caller.
			unsafe { floats(ptr, count * 16) }
				.as_chunks::<16>()
				.0
				.iter()
				.map(|chunk| {
					let mut matrix = Mat4::IDENTITY;

					for (row, values) in matrix.0.iter_mut().zip(chunk.as_chunks::<4>().0) {
						*row = *values;
					}

					matrix
				})
				.collect::<Vec<_>>()
		})
	};

	let parents = if parents.is_null() {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(parents, count) }.to_vec()
	};

	let rest_pose = (!rest_pose.is_null()).then(|| {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(rest_pose, count) }.to_vec()
	});

	let names = if names.is_null() {
		Vec::new()
	} else {
		// SAFETY: guaranteed by the caller.
		unsafe { std::slice::from_raw_parts(names, count) }
			.iter()
			// SAFETY: guaranteed by the caller.
			.map(|name| unsafe { string_from(*name) })
			.collect()
	};

	let data = SkeletonData::new(
		joint_count,
		matrices(inverse_bind),
		parents,
		rest_pose,
		matrices(root_transforms),
		names,
	);

	Box::into_raw(Box::new(Skeleton::new(data))).cast()
}

/// # Safety
///
/// `skeleton` must be null or come from this module, and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_free(skeleton: *mut RxSkeleton)
{
	if !skeleton.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(skeleton.cast::<Skeleton>()) });
	}
}

/// Makes a skeleton that shares the joints and animations of `source` but poses and plays them on
/// its own.
///
/// # Safety
///
/// `source` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_create_instance(source: *const RxSkeleton) -> *mut RxSkeleton
{
	// SAFETY: guaranteed by the caller.
	let instance = unsafe { skeleton_ref(source) }.create_instance();

	Box::into_raw(Box::new(instance)).cast()
}

/// # Safety
///
/// `skeleton` must be live and `name` a string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_find_animation(
	skeleton: *const RxSkeleton,
	name: *const c_char,
) -> AnimationId
{
	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_ref(skeleton).find_animation(&string_from(name)) }
}

/// # Safety
///
/// `skeleton` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_set_rest_animation(
	skeleton: *mut RxSkeleton,
	animation: AnimationId,
	speed: f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_mut(skeleton) }.set_rest_animation(animation, speed);
}

/// # Safety
///
/// `skeleton` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_push_animation(
	skeleton: *mut RxSkeleton,
	animation: AnimationId,
	on_end: u8,
	speed: f32,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { skeleton_mut(skeleton) }.push_animation(
		animation,
		AnimationEnd::from_u8(on_end),
		speed,
	))
}

/// # Safety
///
/// `skeleton` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_stack_is_full(skeleton: *const RxSkeleton) -> u8
{
	// SAFETY: guaranteed by the caller.
	u8::from(unsafe { skeleton_ref(skeleton) }.stack_is_full())
}

/// # Safety
///
/// `skeleton` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_pop_animation(skeleton: *mut RxSkeleton)
{
	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_mut(skeleton) }.pop_animation();
}

/// # Safety
///
/// `skeleton` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_clear_animation_stack(skeleton: *mut RxSkeleton)
{
	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_mut(skeleton) }.clear_animation_stack();
}

/// Writes the playback on top of the stack, or the rest animation's if the stack is empty, and
/// returns 0 if there is neither.
///
/// # Safety
///
/// `skeleton` must be live and `out` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_active_playback(
	skeleton: *const RxSkeleton,
	out: *mut RxPlayback,
) -> u8
{
	// SAFETY: guaranteed by the caller.
	match unsafe { skeleton_ref(skeleton) }.active_playback() {
		Some(playback) => {
			// SAFETY: guaranteed by the caller.
			unsafe { out.write(playback) };
			1
		}
		None => 0,
	}
}

/// # Safety
///
/// `skeleton` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_set_external_pose(skeleton: *mut RxSkeleton, enabled: u8)
{
	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_mut(skeleton) }.set_external_pose(enabled != 0);
}

/// # Safety
///
/// `skeleton` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_evaluate_pose(
	skeleton: *mut RxSkeleton,
	animation: AnimationId,
	time: f32,
)
{
	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_mut(skeleton) }.evaluate_pose(animation, time);
}

/// # Safety
///
/// `skeleton` must be live. `driven_world` is `joint_count` row-major matrices and `is_driven`
/// `joint_count` flags.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_pose_from_driven_bones(
	skeleton: *mut RxSkeleton,
	driven_world: *const f32,
	is_driven: *const u8,
)
{
	// SAFETY: guaranteed by the caller.
	let skeleton = unsafe { skeleton_mut(skeleton) };
	let count = skeleton.joint_count() as usize;

	// SAFETY: guaranteed by the caller.
	let (driven, flags) = unsafe {
		(
			std::slice::from_raw_parts(driven_world.cast::<Mat4>(), count),
			std::slice::from_raw_parts(is_driven, count),
		)
	};

	skeleton.pose_from_driven_bones(driven, flags);
}

/// # Safety
///
/// `skeleton` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_advance(skeleton: *mut RxSkeleton, delta_time: f32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_mut(skeleton) }.advance(delta_time);
}

/// # Safety
///
/// `skeleton` must be live and `name` a string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_find_bone(
	skeleton: *const RxSkeleton,
	name: *const c_char,
) -> u32
{
	if name.is_null() {
		return raptor_anim::NO_BONE;
	}

	// SAFETY: guaranteed by the caller.
	let name = unsafe { CStr::from_ptr(name) };

	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_ref(skeleton) }.find_bone(name.to_bytes())
}

/// The name of a joint, or null if there is no such joint.
///
/// # Safety
///
/// `skeleton` must be live. The name lasts as long as the skeleton and the instances made from it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_skeleton_bone_name(
	skeleton: *const RxSkeleton,
	bone: u32,
) -> *const c_char
{
	// SAFETY: guaranteed by the caller.
	unsafe { skeleton_ref(skeleton) }
		.bone_name(bone)
		.map_or(std::ptr::null(), |name| name.as_ptr())
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_skeleton_max_animation_stack() -> u32
{
	MAX_ANIMATION_STACK as u32
}
