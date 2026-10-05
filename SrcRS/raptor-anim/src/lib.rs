pub mod animation;
pub mod math;
pub mod skeleton;

pub use animation::{Animation, BoneTrack, Track};
pub use math::{Mat4, Quat};
pub use skeleton::{
	AnimationEnd, AnimationId, MAX_ANIMATION_STACK, NO_ANIMATION, NO_BONE, Playback, RestPose,
	Skeleton, SkeletonData, SkeletonFields,
};
