use std::ffi::CString;
use std::sync::Arc;

use crate::animation::Animation;
use crate::math::{self, Mat4, QUAT_IDENTITY, Quat};

pub type AnimationId = u32;

pub const NO_ANIMATION: AnimationId = u32::MAX;
pub const NO_BONE: u32 = u32::MAX;
pub const MAX_ANIMATION_STACK: usize = 8;

const POSE_HASH_BASIS: u32 = 2_166_136_261;
const POSE_HASH_PRIME: u32 = 16_777_619;

/// What an animation does once it reaches its end.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum AnimationEnd
{
	/// Removed from the stack, handing control back to what is beneath it
	Pop = 0,
	/// Stays on the last frame until it is popped
	Hold = 1,
	/// Wraps back around to the start
	Loop = 2,
}

impl AnimationEnd
{
	pub fn from_u8(value: u8) -> Self
	{
		match value {
			1 => Self::Hold,
			2 => Self::Loop,
			_ => Self::Pop,
		}
	}
}

/// An animation being played, and how far into it playback is.
#[derive(Clone, Copy, Debug, PartialEq)]
#[repr(C)]
pub struct Playback
{
	pub animation: AnimationId,
	pub time: f32,
	pub speed: f32,
	pub on_end: u8,
}

impl Playback
{
	const NONE: Self = Self {
		animation: NO_ANIMATION,
		time: 0.0,
		speed: 1.0,
		on_end: AnimationEnd::Loop as u8,
	};
}

/// The local transform a joint sits at when an animation does not drive it. Each part is padded to
/// four floats like the engine's vector types, so this matches `BoneRestPose` in memory.
#[derive(Clone, Copy, Debug, PartialEq)]
#[repr(C)]
pub struct RestPose
{
	pub translation: [f32; 4],
	pub rotation: Quat,
	pub scale: [f32; 4],
}

impl Default for RestPose
{
	fn default() -> Self
	{
		Self {
			translation: [0.0; 4],
			rotation: QUAT_IDENTITY,
			scale: [1.0, 1.0, 1.0, 0.0],
		}
	}
}

/// What every instance of a skeleton shares.
#[derive(Default)]
pub struct SkeletonData
{
	joint_count: u32,
	inverse_bind: Vec<Mat4>,
	rest_pose: Vec<RestPose>,
	root_transforms: Vec<Mat4>,
	parents: Vec<u32>,
	names: Vec<CString>,
	order: Vec<u32>,
	animations: Vec<Animation>,
}

/// The state the engine reads straight out of a skeleton. It has to stay at the start of
/// [`Skeleton`], and the pointers stay valid for as long as the skeleton lives.
#[repr(C)]
pub struct SkeletonFields
{
	pub joint_count: u32,
	pub pose_hash: u32,
	pub pose_radius: f32,
	pub reserved: u32,
	pub pose_center: [f32; 4],
	pub local: *const Mat4,
	pub world: *const Mat4,
	pub skinning: *const Mat4,
	pub parents: *const u32,
}

#[repr(C)]
pub struct Skeleton
{
	fields: SkeletonFields,
	data: Arc<SkeletonData>,
	local: Vec<Mat4>,
	world: Vec<Mat4>,
	skinning: Vec<Mat4>,
	rest_playback: Playback,
	stack: Vec<Playback>,
	external_pose: bool,
	holding_rest_pose: bool,
}

/// Parents first, so that a joint's world transform can be built from its parent's. A joint whose
/// parent is missing or part of a loop counts as a root.
fn parent_first_order(parents: &mut [u32]) -> Vec<u32>
{
	let count = parents.len();

	for (index, parent) in parents.iter_mut().enumerate() {
		if *parent != NO_BONE && (*parent as usize >= count || *parent as usize == index) {
			*parent = NO_BONE;
		}
	}

	let mut order = Vec::with_capacity(count);
	let mut placed = vec![false; count];

	while order.len() < count {
		let before = order.len();

		for index in 0..count {
			let parent = parents[index];

			if !placed[index] && (parent == NO_BONE || placed[parent as usize]) {
				placed[index] = true;
				order.push(index as u32);
			}
		}

		if order.len() == before {
			for index in 0..count {
				if !placed[index] {
					parents[index] = NO_BONE;
				}
			}
		}
	}

	order
}

impl SkeletonData
{
	/// `inverse_bind`, `rest_pose` and `root_transforms` may be left out, and each joint then gets
	/// the identity, the default rest pose, or no root transform.
	pub fn new(
		joint_count: u32,
		inverse_bind: Option<Vec<Mat4>>,
		mut parents: Vec<u32>,
		rest_pose: Option<Vec<RestPose>>,
		root_transforms: Option<Vec<Mat4>>,
		names: Vec<String>,
	) -> Self
	{
		let count = joint_count as usize;

		parents.resize(count, NO_BONE);

		let order = parent_first_order(&mut parents);

		let mut names: Vec<CString> = names
			.into_iter()
			.map(|name| CString::new(name.replace('\0', "")).unwrap_or_default())
			.collect();

		names.resize(count, CString::default());

		Self {
			joint_count,
			inverse_bind: inverse_bind.unwrap_or_else(|| vec![Mat4::IDENTITY; count]),
			rest_pose: rest_pose.unwrap_or_else(|| vec![RestPose::default(); count]),
			root_transforms: root_transforms.unwrap_or_else(|| vec![Mat4::IDENTITY; count]),
			parents,
			names,
			order,
			animations: Vec::new(),
		}
	}

	pub fn joint_count(&self) -> u32
	{
		self.joint_count
	}

	pub fn animations(&self) -> &[Animation]
	{
		&self.animations
	}
}

fn fold_hash(hash: u32, word: u32) -> u32
{
	(hash ^ word).wrapping_mul(POSE_HASH_PRIME)
}

impl Skeleton
{
	pub fn new(data: SkeletonData) -> Self
	{
		Self::from_shared(Arc::new(data), Playback::NONE)
	}

	fn from_shared(data: Arc<SkeletonData>, rest_playback: Playback) -> Self
	{
		let count = data.joint_count as usize;

		let mut skeleton = Self {
			fields: SkeletonFields {
				joint_count: data.joint_count,
				pose_hash: 0,
				pose_radius: 0.0,
				reserved: 0,
				pose_center: [0.0; 4],
				local: std::ptr::null(),
				world: std::ptr::null(),
				skinning: std::ptr::null(),
				parents: data.parents.as_ptr(),
			},
			local: vec![Mat4::IDENTITY; count],
			world: vec![Mat4::IDENTITY; count],
			skinning: vec![Mat4::IDENTITY; count],
			data,
			rest_playback,
			stack: Vec::with_capacity(MAX_ANIMATION_STACK),
			external_pose: false,
			holding_rest_pose: false,
		};

		skeleton.fields.local = skeleton.local.as_ptr();
		skeleton.fields.world = skeleton.world.as_ptr();
		skeleton.fields.skinning = skeleton.skinning.as_ptr();

		skeleton
	}

	/// A skeleton that shares this one's joints and animations but poses and plays them on its own.
	pub fn create_instance(&self) -> Self
	{
		let mut rest_playback = self.rest_playback;
		rest_playback.time = 0.0;

		let mut instance = Self::from_shared(Arc::clone(&self.data), rest_playback);

		if instance.data.joint_count > 0 {
			instance.evaluate_pose(instance.rest_playback.animation, 0.0);
		}

		instance
	}

	pub fn fields(&self) -> &SkeletonFields
	{
		&self.fields
	}

	pub fn data(&self) -> &SkeletonData
	{
		&self.data
	}

	pub fn joint_count(&self) -> u32
	{
		self.data.joint_count
	}

	pub fn bone_name(&self, bone: u32) -> Option<&CString>
	{
		self.data.names.get(bone as usize)
	}

	pub fn find_bone(&self, name: &[u8]) -> u32
	{
		self.data
			.names
			.iter()
			.position(|candidate| candidate.as_bytes() == name)
			.map_or(NO_BONE, |index| index as u32)
	}

	pub fn skinning_matrices(&self) -> &[Mat4]
	{
		&self.skinning
	}

	pub fn world_transforms(&self) -> &[Mat4]
	{
		&self.world
	}

	/// Adds an animation to a skeleton that no instance has been made from yet, or gives it back if
	/// that is no longer so.
	pub fn add_animation(&mut self, animation: Animation) -> Result<AnimationId, Animation>
	{
		match Arc::get_mut(&mut self.data) {
			Some(data) => {
				data.animations.push(animation);
				Ok((data.animations.len() - 1) as AnimationId)
			}
			None => Err(animation),
		}
	}

	pub fn find_animation(&self, name: &str) -> AnimationId
	{
		self.data
			.animations
			.iter()
			.position(|animation| animation.name == name)
			.map_or(NO_ANIMATION, |index| index as AnimationId)
	}

	fn animation(&self, id: AnimationId) -> Option<&Animation>
	{
		self.data.animations.get(id as usize)
	}

	/// Sets the animation that loops whenever the stack is empty. Passing none holds the rest pose.
	pub fn set_rest_animation(&mut self, animation: AnimationId, speed: f32)
	{
		let animation = self.valid_or_none(animation);

		self.rest_playback = Playback {
			animation,
			time: 0.0,
			speed,
			on_end: AnimationEnd::Loop as u8,
		};
		self.holding_rest_pose = false;
	}

	fn valid_or_none(&self, animation: AnimationId) -> AnimationId
	{
		if self.animation(animation).is_some() {
			animation
		}
		else {
			NO_ANIMATION
		}
	}

	/// Plays `animation` over what is playing. It fails if there is no such animation or the stack
	/// is full.
	pub fn push_animation(&mut self, animation: AnimationId, on_end: AnimationEnd, speed: f32)
	-> bool
	{
		if self.animation(animation).is_none() || self.stack.len() >= MAX_ANIMATION_STACK {
			return false;
		}

		self.stack.push(Playback {
			animation,
			time: 0.0,
			speed,
			on_end: on_end as u8,
		});
		self.holding_rest_pose = false;

		true
	}

	pub fn stack_is_full(&self) -> bool
	{
		self.stack.len() >= MAX_ANIMATION_STACK
	}

	pub fn pop_animation(&mut self)
	{
		self.stack.pop();
	}

	pub fn clear_animation_stack(&mut self)
	{
		self.stack.clear();
	}

	/// The top of the stack, or the rest animation if the stack is empty.
	pub fn active_playback(&self) -> Option<Playback>
	{
		self.stack.last().copied().or_else(|| {
			(self.rest_playback.animation != NO_ANIMATION).then_some(self.rest_playback)
		})
	}

	pub fn set_external_pose(&mut self, enabled: bool)
	{
		self.external_pose = enabled;
		self.holding_rest_pose = false;
	}

	/// Poses the skeleton at `time` in `animation`, or in its rest pose if there is no such
	/// animation, and updates the skinning matrices.
	pub fn evaluate_pose(&mut self, animation: AnimationId, time: f32)
	{
		let data = Arc::clone(&self.data);
		let animation = data.animations.get(animation as usize);

		for (index, local) in self.local.iter_mut().enumerate() {
			let rest = data.rest_pose[index];

			let mut translation = [rest.translation[0], rest.translation[1], rest.translation[2]];
			let mut rotation = rest.rotation;
			let mut scale = [rest.scale[0], rest.scale[1], rest.scale[2]];

			if let Some(track) = animation.and_then(|animation| animation.tracks.get(index)) {
				translation = track.translation_at(time, translation);
				rotation = track.rotation_at(time, rotation);
				scale = track.scale_at(time, scale);
			}

			*local = math::multiply(
				&math::multiply(&Mat4::scale(scale), &Mat4::rotation(rotation)),
				&Mat4::translation(translation),
			);
		}

		for &index in &data.order {
			let index = index as usize;
			let parent = data.parents[index];

			self.world[index] = if parent == NO_BONE {
				math::multiply(&self.local[index], &data.root_transforms[index])
			}
			else {
				math::multiply(&self.local[index], &self.world[parent as usize])
			};
		}

		for (index, skinning) in self.skinning.iter_mut().enumerate() {
			*skinning = math::multiply(&data.inverse_bind[index], &self.world[index]);
		}

		self.update_pose_summary();
	}

	/// Poses the skeleton with the world transforms of the joints something else drives (a
	/// ragdoll), and the rest from their local transforms beneath them.
	pub fn pose_from_driven_bones(&mut self, driven_world: &[Mat4], is_driven: &[u8])
	{
		let data = Arc::clone(&self.data);

		for &index in &data.order {
			let index = index as usize;
			let parent = data.parents[index];

			self.world[index] = if is_driven.get(index).copied().unwrap_or(0) != 0 {
				driven_world.get(index).copied().unwrap_or(Mat4::IDENTITY)
			}
			else if parent == NO_BONE {
				math::multiply(&self.local[index], &data.root_transforms[index])
			}
			else {
				math::multiply(&self.local[index], &self.world[parent as usize])
			};

			self.skinning[index] = math::multiply(&data.inverse_bind[index], &self.world[index]);
		}

		self.update_pose_summary();
	}

	/// Plays the active animation for `delta_time` more seconds and poses the skeleton, then deals
	/// with the animation reaching its end.
	pub fn advance(&mut self, delta_time: f32)
	{
		let playback = if self.external_pose {
			None
		}
		else {
			self.active_playback()
		};

		let Some(playback) = playback else {
			if !self.external_pose && !self.holding_rest_pose {
				self.evaluate_pose(NO_ANIMATION, 0.0);
				self.holding_rest_pose = true;
			}

			return;
		};

		self.evaluate_pose(playback.animation, playback.time);

		let duration = self
			.animation(playback.animation)
			.map_or(0.0, |animation| animation.duration);

		let time = delta_time.mul_add(playback.speed, playback.time);
		let on_the_stack = !self.stack.is_empty();

		let mut finished = AnimationEnd::from_u8(playback.on_end);
		let mut new_time = time;

		if time >= duration {
			if !on_the_stack {
				finished = AnimationEnd::Loop;
			}

			match finished {
				AnimationEnd::Pop => {}
				AnimationEnd::Hold => new_time = duration,
				AnimationEnd::Loop => {
					new_time = if duration > 0.0 { time % duration } else { 0.0 };
				}
			}
		}

		let active = if on_the_stack {
			self.stack.last_mut()
		}
		else {
			Some(&mut self.rest_playback)
		};

		if let Some(active) = active {
			active.time = new_time;
		}

		if time >= duration && on_the_stack && finished == AnimationEnd::Pop {
			self.pop_animation();
		}
	}

	fn update_pose_summary(&mut self)
	{
		if self.world.is_empty() {
			return;
		}

		let mut hash = POSE_HASH_BASIS;

		for word in self.skinning.iter().flat_map(Mat4::words) {
			hash = fold_hash(hash, word);
		}

		let mut min = [1.0e30_f32; 3];
		let mut max = [-1.0e30_f32; 3];

		for matrix in &self.world {
			let position = matrix.translation_part();

			for axis in 0..3 {
				if position[axis] < min[axis] {
					min[axis] = position[axis];
				}

				if position[axis] > max[axis] {
					max[axis] = position[axis];
				}
			}
		}

		self.fields.pose_hash = hash;
		self.fields.pose_center = [
			(min[0] + max[0]) * 0.5,
			(min[1] + max[1]) * 0.5,
			(min[2] + max[2]) * 0.5,
			0.0,
		];
		self.fields.pose_radius = math::half_diagonal(min, max);
	}
}

#[cfg(test)]
mod tests
{
	use super::*;
	use crate::animation::{BoneTrack, Track};

	fn chain(count: u32) -> SkeletonData
	{
		let parents = (0..count)
			.map(|index| if index == 0 { NO_BONE } else { index - 1 })
			.collect();

		let rest = (0..count)
			.map(|_| RestPose {
				translation: [1.0, 0.0, 0.0, 0.0],
				..RestPose::default()
			})
			.collect();

		SkeletonData::new(
			count,
			None,
			parents,
			Some(rest),
			None,
			(0..count).map(|index| format!("bone_{index}")).collect(),
		)
	}

	fn slide(duration: f32, bones: usize) -> Animation
	{
		let track = || BoneTrack {
			translation: Track {
				times: vec![0.0, duration],
				values: vec![[0.0; 3], [10.0, 0.0, 0.0]],
			},
			..BoneTrack::default()
		};

		Animation {
			name: "slide".to_owned(),
			duration,
			tracks: (0..bones).map(|_| track()).collect(),
		}
	}

	fn skeleton_with_animations() -> (Skeleton, AnimationId, AnimationId)
	{
		let mut skeleton = Skeleton::new(chain(3));
		let one = skeleton.add_animation(slide(1.0, 3)).unwrap();
		let two = skeleton
			.add_animation(Animation {
				name: "other".to_owned(),
				..slide(2.0, 3)
			})
			.unwrap();

		(skeleton, one, two)
	}

	#[test]
	fn the_rest_pose_stacks_each_joint_on_its_parent()
	{
		let mut skeleton = Skeleton::new(chain(3));

		skeleton.evaluate_pose(NO_ANIMATION, 0.0);

		let positions: Vec<f32> = skeleton
			.world_transforms()
			.iter()
			.map(|matrix| matrix.translation_part()[0])
			.collect();

		assert_eq!(positions, vec![1.0, 2.0, 3.0]);
	}

	#[test]
	fn the_skinning_matrix_undoes_the_bind_pose()
	{
		let inverse_bind = Mat4::translation([-1.0, 0.0, 0.0]);

		let data = SkeletonData::new(
			1,
			Some(vec![inverse_bind]),
			vec![NO_BONE],
			None,
			None,
			vec!["root".to_owned()],
		);

		let mut posed = Skeleton::new(data);
		posed.evaluate_pose(NO_ANIMATION, 0.0);

		assert_eq!(posed.skinning_matrices()[0], inverse_bind);
	}

	#[test]
	fn a_joint_listed_before_its_parent_is_posed_after_it()
	{
		let data = SkeletonData::new(
			2,
			None,
			vec![1, NO_BONE],
			Some(vec![
				RestPose {
					translation: [1.0, 0.0, 0.0, 0.0],
					..RestPose::default()
				},
				RestPose {
					translation: [5.0, 0.0, 0.0, 0.0],
					..RestPose::default()
				},
			]),
			None,
			vec!["child".to_owned(), "parent".to_owned()],
		);

		let mut skeleton = Skeleton::new(data);
		skeleton.evaluate_pose(NO_ANIMATION, 0.0);

		assert_eq!(skeleton.world_transforms()[0].translation_part()[0], 6.0);
	}

	#[test]
	fn a_parent_loop_turns_into_roots_instead_of_hanging()
	{
		let data = SkeletonData::new(2, None, vec![1, 0], None, None, vec![]);

		let mut skeleton = Skeleton::new(data);
		skeleton.evaluate_pose(NO_ANIMATION, 0.0);

		assert_eq!(skeleton.joint_count(), 2);
	}

	#[test]
	fn an_animation_moves_the_joints_it_has_tracks_for()
	{
		let (mut skeleton, slide, _) = skeleton_with_animations();

		skeleton.evaluate_pose(slide, 0.5);

		assert_eq!(skeleton.world_transforms()[0].translation_part()[0], 5.0);
		assert_eq!(skeleton.world_transforms()[2].translation_part()[0], 15.0);
	}

	#[test]
	fn animations_can_only_be_added_before_an_instance_exists()
	{
		let (mut skeleton, _, _) = skeleton_with_animations();
		let _instance = skeleton.create_instance();

		assert!(skeleton.add_animation(slide(1.0, 3)).is_err());
	}

	#[test]
	fn instances_pose_and_play_independently()
	{
		let (skeleton, slide, _) = skeleton_with_animations();

		let mut a = skeleton.create_instance();
		let b = skeleton.create_instance();

		a.set_rest_animation(slide, 1.0);
		a.advance(0.5);

		assert_ne!(
			a.world_transforms()[0].translation_part(),
			b.world_transforms()[0].translation_part()
		);
		assert_eq!(a.find_animation("slide"), b.find_animation("slide"));
	}

	#[test]
	fn the_rest_animation_loops_when_nothing_is_stacked()
	{
		let (mut skeleton, slide, _) = skeleton_with_animations();

		skeleton.set_rest_animation(slide, 1.0);
		skeleton.advance(0.75);
		skeleton.advance(0.75);

		let time = skeleton.active_playback().unwrap().time;

		assert!((time - 0.5).abs() < 1e-6, "{time}");
	}

	#[test]
	fn a_one_shot_is_popped_when_it_ends_and_the_rest_animation_resumes()
	{
		let (mut skeleton, slide, other) = skeleton_with_animations();

		skeleton.set_rest_animation(other, 1.0);
		assert!(skeleton.push_animation(slide, AnimationEnd::Pop, 1.0));

		assert_eq!(skeleton.active_playback().unwrap().animation, slide);

		skeleton.advance(0.6);
		skeleton.advance(0.6);

		assert_eq!(skeleton.active_playback().unwrap().animation, other);
	}

	#[test]
	fn a_held_animation_stays_on_its_last_frame()
	{
		let (mut skeleton, slide, _) = skeleton_with_animations();

		assert!(skeleton.push_animation(slide, AnimationEnd::Hold, 1.0));

		for _ in 0..5 {
			skeleton.advance(0.5);
		}

		let playback = skeleton.active_playback().unwrap();

		assert_eq!(playback.animation, slide);
		assert_eq!(playback.time, 1.0);
		assert_eq!(skeleton.world_transforms()[0].translation_part()[0], 10.0);
	}

	#[test]
	fn a_looping_animation_wraps()
	{
		let (mut skeleton, slide, _) = skeleton_with_animations();

		assert!(skeleton.push_animation(slide, AnimationEnd::Loop, 1.0));
		skeleton.advance(0.75);
		skeleton.advance(0.75);

		assert!((skeleton.active_playback().unwrap().time - 0.5).abs() < 1e-6);
	}

	#[test]
	fn the_stack_stops_taking_animations_when_it_is_full()
	{
		let (mut skeleton, slide, _) = skeleton_with_animations();

		for _ in 0..MAX_ANIMATION_STACK {
			assert!(skeleton.push_animation(slide, AnimationEnd::Hold, 1.0));
		}

		assert!(skeleton.stack_is_full());
		assert!(!skeleton.push_animation(slide, AnimationEnd::Hold, 1.0));
		assert!(!skeleton.push_animation(NO_ANIMATION, AnimationEnd::Hold, 1.0));
	}

	#[test]
	fn with_nothing_playing_the_rest_pose_is_held()
	{
		let mut skeleton = Skeleton::new(chain(2));

		skeleton.advance(0.1);

		assert_eq!(skeleton.world_transforms()[1].translation_part()[0], 2.0);
		assert!(skeleton.active_playback().is_none());
	}

	#[test]
	fn an_external_pose_is_left_alone_by_advance()
	{
		let (mut skeleton, slide, _) = skeleton_with_animations();

		skeleton.set_rest_animation(slide, 1.0);
		skeleton.advance(0.5);
		skeleton.set_external_pose(true);

		let before = skeleton.world_transforms().to_vec();

		skeleton.advance(0.25);

		assert_eq!(skeleton.world_transforms(), &before[..]);
	}

	#[test]
	fn driven_bones_take_their_world_transform_and_carry_their_children()
	{
		let mut skeleton = Skeleton::new(chain(3));
		skeleton.evaluate_pose(NO_ANIMATION, 0.0);

		let driven = vec![
			Mat4::IDENTITY,
			Mat4::translation([100.0, 0.0, 0.0]),
			Mat4::IDENTITY,
		];

		skeleton.pose_from_driven_bones(&driven, &[0, 1, 0]);

		assert_eq!(skeleton.world_transforms()[1].translation_part()[0], 100.0);
		assert_eq!(skeleton.world_transforms()[2].translation_part()[0], 101.0);
	}

	#[test]
	fn the_pose_summary_covers_the_joints()
	{
		let mut skeleton = Skeleton::new(chain(3));
		skeleton.evaluate_pose(NO_ANIMATION, 0.0);

		let fields = skeleton.fields();

		assert_eq!(fields.pose_center[0], 2.0);
		assert_eq!(fields.pose_radius, 1.0);
		assert_ne!(fields.pose_hash, 0);
	}

	#[test]
	fn the_pose_hash_changes_with_the_pose()
	{
		let (mut skeleton, slide, _) = skeleton_with_animations();

		skeleton.evaluate_pose(slide, 0.1);
		let first = skeleton.fields().pose_hash;

		skeleton.evaluate_pose(slide, 0.2);

		assert_ne!(first, skeleton.fields().pose_hash);
	}

	#[test]
	fn bones_are_found_by_name()
	{
		let skeleton = Skeleton::new(chain(3));

		assert_eq!(skeleton.find_bone(b"bone_2"), 2);
		assert_eq!(skeleton.find_bone(b"missing"), NO_BONE);
	}

	#[test]
	fn the_shared_structs_have_the_layout_the_header_declares()
	{
		use std::mem::{align_of, offset_of, size_of};

		assert_eq!(size_of::<SkeletonFields>(), 64);
		assert_eq!(offset_of!(SkeletonFields, pose_center), 16);
		assert_eq!(offset_of!(SkeletonFields, local), 32);
		assert_eq!(offset_of!(SkeletonFields, world), 40);
		assert_eq!(offset_of!(SkeletonFields, skinning), 48);
		assert_eq!(offset_of!(SkeletonFields, parents), 56);

		assert_eq!(size_of::<RestPose>(), 48);
		assert_eq!(size_of::<Playback>(), 16);
		assert_eq!(offset_of!(Playback, on_end), 12);
		assert_eq!((size_of::<Mat4>(), align_of::<Mat4>()), (64, 16));
		assert_eq!(offset_of!(Skeleton, fields), 0);
	}

	#[test]
	fn the_fields_point_at_the_matrices()
	{
		let skeleton = Skeleton::new(chain(2));

		assert_eq!(skeleton.fields().world, skeleton.world_transforms().as_ptr());
		assert_eq!(skeleton.fields().skinning, skeleton.skinning_matrices().as_ptr());
	}
}
