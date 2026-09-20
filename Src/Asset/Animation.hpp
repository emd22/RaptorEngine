#pragma once

#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/StackArray.hpp>
#include <Core/String.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Limits.hpp>

namespace fx {

using BoneId = uint32;
static constexpr BoneId BoneNull = UINT32_MAX;

template <typename T>
struct BoneTransformTrack
{
	SizedArray<float32> Times;
	SizedArray<T> Values;
};

struct BoneTrack
{
	BoneTransformTrack<Vec3f> Translation;
	BoneTransformTrack<Quat> Rotation;
	BoneTransformTrack<Vec3f> Scale;
};

struct Animation
{
	String Name;
	float32 Duration = 0.0f;
	SizedArray<BoneTrack> BoneTracks;
};

/// What an animation on the stack does once it reaches its end.
enum class eAnimationEnd : uint8
{
	/// Remove it from the stack, handing control back to whatever is beneath it (or the rest animation). For one-shots
	/// such as firing or reloading.
	Pop,
	/// Stay on the last frame until popped.
	Hold,
	/// Wrap back around to the start.
	Loop,
};

/// An animation being played on a skeleton, along with how far into it playback is.
struct AnimationPlayback
{
	const Animation* pAnimation = nullptr;
	float32 Time = 0.0f;
	float32 Speed = 1.0f;
	eAnimationEnd OnEnd = eAnimationEnd::Pop;
};

struct BoneTransform
{
	BoneTransform() = default;
	BoneTransform(const fx::Vec3f& position, const Quat& rotation) : Position(position), Rotation(rotation) {}

	Vec3f Position = Vec3f::sZero;
	Quat Rotation = Quat::scIdentity;
};

/// The local transform a joint sits at when an animation does not drive it.
///
/// GLTF animations only contain channels for the joints (and the components of those joints) that
/// actually move, so anything not present in an animation has to fall back to the joint node's own
/// transform. Falling back to identity instead collapses every bone onto its parent's origin.
struct BoneRestPose
{
	Vec3f Translation = Vec3f::sZero;
	Quat Rotation = Quat::scIdentity;
	Vec3f Scale = Vec3f::sOne;
};

struct Skeleton
{
public:
	static constexpr uint32 scNoBones = UINT32_MAX;
	static constexpr uint32 scMaxAnimationStack = 8;

public:
	const Animation* FindAnimation(const String& name) const;

	/**
	 * @brief Sets the animation that loops whenever the stack is empty. Pass null to hold the rest (bind) pose instead.
	 */
	void SetRestAnimation(const Animation* anim, float32 speed = 1.0f);

	/**
	 * @brief Plays `anim` on top of whatever is currently playing. The animation beneath it is paused, and resumes from
	 * where it left off once this one is popped.
	 *
	 * @returns false if `anim` is null or the stack is full.
	 */
	bool PushAnimation(const Animation* anim, eAnimationEnd on_end = eAnimationEnd::Pop, float32 speed = 1.0f);
	bool PushAnimation(const String& name, eAnimationEnd on_end = eAnimationEnd::Pop, float32 speed = 1.0f);

	void PopAnimation();
	void ClearAnimationStack();

	/// The top of the animation stack, or the rest animation if the stack is empty. Null when holding the rest pose.
	AnimationPlayback* GetActivePlayback();
	const Animation* GetActiveAnimation();

	/**
	 * @brief Advances the active animation by `delta_time`, poses the skeleton and uploads the pose to the bone buffer.
	 * Only runs once per frame, however many objects or passes share the skeleton.
	 */
	void Update(float32 delta_time);

	/**
	 * @brief Poses the skeleton at `time` in `anim`, or in its rest pose if `anim` is null, and updates
	 * `SkinningMatrices`.
	 */
	void EvaluatePose(const Animation* anim, float32 time);
	BoneTransform GetBoneTransform(const Ref<Animation>& anim, float32 time, BoneId bone_id) const;
	Mat4f GetBoneTransformMatrix(const Ref<Animation>& anim, float32 time, BoneId bone_id) const;

	BoneId FindBone(const Ref<Animation>& anim, const String& name) const;

public:
	SizedArray<Mat4f> InvBindTransforms;
	SizedArray<BoneRestPose> RestPose;

	SizedArray<Mat4f> RootTransforms;
	SizedArray<uint32> ParentIndices;
	SizedArray<String> BoneNames;
	uint32 JointCount = 0;

	SizedArray<Mat4f> LocalTransforms;
	SizedArray<Mat4f> WorldTransforms;
	SizedArray<Mat4f> SkinningMatrices;

	/// Animations and playback state live on the skeleton rather than the object, as a single GLTF skin is commonly
	/// shared by several meshes (each loaded as its own object). Those objects share this skeleton through `Ref`, so
	/// the pose is evaluated and uploaded once per frame and every mesh draws with the same bone-buffer slot.
	SizedArray<Animation> Animations;

	/// Plays while the animation stack is empty. Holds the rest pose if it has no animation.
	AnimationPlayback RestPlayback { .OnEnd = eAnimationEnd::Loop };
	/// Animations pushed over the rest animation. Only the top one advances.
	StackArray<AnimationPlayback, scMaxAnimationStack> AnimationStack;

	uint32 BoneBufferBase = scNoBones;

	uint32 LastUpdateFrame = UINT32_MAX;

private:
	/// Set once the rest pose has been evaluated, as it never changes and only needs posing once.
	bool mbHoldingRestPose = false;
};

} // namespace fx
