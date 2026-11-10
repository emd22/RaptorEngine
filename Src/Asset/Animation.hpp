#pragma once

#include <Core/Name.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/StackArray.hpp>
#include <Core/String.hpp>
#include <Math/FMat4.hpp>
#include <Math/FQuat.hpp>
#include <Math/FVec3.hpp>
#include <Renderer/Limits.hpp>

namespace fx {

using BoneId = uint32;
static constexpr BoneId scBoneNull = UINT32_MAX;

enum class eAnimationInterpolation : uint8
{
	Linear,
	Step,
	CubicSpline,
};

template <typename T>
struct BoneTransformTrack
{
	SizedArray<float32> Times;
	SizedArray<T> Values;

	SizedArray<T> InTangents;
	SizedArray<T> OutTangents;

	eAnimationInterpolation Interpolation = eAnimationInterpolation::Linear;
};

struct BoneTrack
{
	BoneTransformTrack<FVec3> Translation;
	BoneTransformTrack<FQuat> Rotation;
	BoneTransformTrack<FVec3> Scale;
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
	float32 BlendTime = 0.0f;
};

struct BoneTransform
{
	FVec3 Translation = FVec3::sZero;
	FQuat Rotation = FQuat::scIdentity;
	FVec3 Scale = FVec3::sOne;
};

struct BoneDelta
{
	FVec3 Translation = FVec3::sZero;
	FVec3 Rotation = FVec3::sZero;
	FVec3 Scale = FVec3::sZero;
};

void BlendPoses(const BoneTransform* a, const BoneTransform* b, float32 weight, BoneTransform* out, uint32 count);

// TODO: Move Skeleton into its own file.


struct Skeleton
{
public:
	static constexpr uint32 scNoBones = UINT32_MAX;
	static constexpr uint32 scMaxAnimationStack = 8;
	static constexpr float32 scDefaultBlendTime = 0.2f;

public:
	static Ref<Skeleton> CreateInstance(const Ref<Skeleton>& source);

	const Animation* FindAnimation(const String& name) const;

	/**
	 * @brief Sets the animation that loops whenever the stack is empty. Pass null to hold the rest (bind) pose instead.
	 */
	void SetRestAnimation(const Animation* anim, float32 speed = 1.0f, float32 blend_time = scDefaultBlendTime);

	/**
	 * @brief Plays `anim` on top of whatever is currently playing. The animation beneath it is paused, and resumes from
	 * where it left off once this one is popped.
	 *
	 * @returns false if `anim` is null or the stack is full.
	 */
	bool PushAnimation(const Animation* anim, eAnimationEnd on_end = eAnimationEnd::Pop, float32 speed = 1.0f,
					   float32 blend_time = scDefaultBlendTime);
	bool PushAnimation(const String& name, eAnimationEnd on_end = eAnimationEnd::Pop, float32 speed = 1.0f,
					   float32 blend_time = scDefaultBlendTime);

	void PopAnimation(float32 blend_time = scDefaultBlendTime);
	void ClearAnimationStack(float32 blend_time = scDefaultBlendTime);

	void BeginTransition(float32 blend_time);
	FX_FORCE_INLINE bool IsTransitioning() const { return mBlendDuration > 0.0f; }

	/// The top of the animation stack, or the rest animation if the stack is empty. Null when holding the rest pose.
	AnimationPlayback* GetActivePlayback();
	const Animation* GetActiveAnimation();

	/**
	 * @brief Advances the active animation by `delta_time`, poses the skeleton and uploads the pose to the bone buffer.
	 * Only runs once per frame, however many objects or passes share the skeleton.
	 */
	void Update(float32 delta_time);

	void AdvancePose(float32 delta_time);

	/**
	 * @brief Poses the skeleton at `time` in `anim`, or in its rest pose if `anim` is null, and updates
	 * `SkinningMatrices`.
	 */
	void EvaluatePose(const Animation* anim, float32 time);

	void SampleLocalPose(const Animation* anim, float32 time, BoneTransform* out) const;
	void ComposePose(const BoneTransform* pose);

	void SetExternalPose(bool enabled);
	FX_FORCE_INLINE bool HasExternalPose() const { return mbExternalPose; }

	void PoseFromDrivenBones(const FMat4* driven_world, const uint8* is_driven);

	void BuildEvaluationOrder();


private:
	void UpdatePoseSummary();
	void EnsurePoseBuffers();
	void StartInertialization(const AnimationPlayback* playback, float32 delta_time);
	void ApplyInertialization(float32 delta_time);
	void StoreOutputPose(const BoneTransform* pose, float32 delta_time);


public:
	SizedArray<FMat4> InvBindTransforms;

	SizedArray<BoneTransform> RestPose;

	SizedArray<FMat4> RootTransforms;
	SizedArray<uint32> ParentIndices;
	SizedArray<uint32> EvaluationOrder;

	SizedArray<Name> BoneNames;

	uint32 JointCount = 0;

	SizedArray<FMat4> LocalTransforms;
	SizedArray<FMat4> WorldTransforms;
	SizedArray<FMat4> SkinningMatrices;

	SizedArray<Animation> Animations;

	/// Plays while the animation stack is empty. Holds the rest pose if it has no animation.
	AnimationPlayback RestPlayback { .OnEnd = eAnimationEnd::Loop };

	StackArray<AnimationPlayback, scMaxAnimationStack> AnimationStack;

	uint32 BoneBufferBase = scNoBones;

	uint32 PoseHash = 0;
	FVec3 PoseCenter = FVec3::sZero;
	float32 PoseRadius = 0.0f;

	Ref<Skeleton> pSource { nullptr };

	uint32 LastUpdateFrame = UINT32_MAX;
	uint32 LastPoseFrame = UINT32_MAX;

private:
	bool mbExternalPose = false;

	/// Set once the rest pose has been evaluated, as it never changes and only needs posing once.
	bool mbHoldingRestPose = false;

	SizedArray<BoneTransform> mSourcePose;
	SizedArray<BoneTransform> mScratchPose;
	SizedArray<BoneTransform> mBlendedPose;
	SizedArray<BoneTransform> mOutputPoses[2];
	SizedArray<BoneDelta> mOffsets;
	SizedArray<BoneDelta> mOffsetVelocities;

	uint32 mOutputIndex = 0;
	uint32 mOutputHistory = 0;
	float32 mLastOutputDeltaTime = 0.0f;

	float32 mPendingBlendTime = -1.0f;
	float32 mBlendDuration = 0.0f;
	float32 mBlendElapsed = 0.0f;
};

} // namespace fx
