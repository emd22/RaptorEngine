#pragma once

#include <raptor_ffi.h>

#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Limits.hpp>

namespace fx {

using BoneId = uint32;
static constexpr BoneId BoneNull = UINT32_MAX;

using AnimationId = uint32;
static constexpr AnimationId NoAnimation = UINT32_MAX;

/// What an animation on the stack does once it reaches its end.
enum class eAnimationEnd : uint8
{
	/// Remove it from the stack, handing control back to whatever is beneath it (or the rest animation). For one-shots
	/// such as firing or reloading.
	Pop = RX_ANIMATION_END_POP,
	/// Stay on the last frame until popped.
	Hold = RX_ANIMATION_END_HOLD,
	/// Wrap back around to the start.
	Loop = RX_ANIMATION_END_LOOP,
};

/// An animation being played on a skeleton, along with how far into it playback is.
struct AnimationPlayback
{
	AnimationId Animation = NoAnimation;
	float32 Time = 0.0f;
	float32 Speed = 1.0f;
	eAnimationEnd OnEnd = eAnimationEnd::Pop;
};

struct Skeleton
{
public:
	static constexpr uint32 scNoBones = UINT32_MAX;
	static constexpr uint32 scMaxAnimationStack = 8;

public:
	Skeleton();
	~Skeleton();

	Skeleton(const Skeleton&) = delete;
	Skeleton& operator=(const Skeleton&) = delete;

	static Ref<Skeleton> CreateInstance(const Ref<Skeleton>& source);

	void Adopt(RxSkeleton* skeleton);

	AnimationId FindAnimation(const String& name) const;

	/**
	 * @brief Sets the animation that loops whenever the stack is empty. Pass NoAnimation to hold the rest (bind) pose
	 * instead.
	 */
	void SetRestAnimation(AnimationId animation, float32 speed = 1.0f);

	/**
	 * @brief Plays `animation` on top of whatever is currently playing. The animation beneath it is paused, and
	 * resumes from where it left off once this one is popped.
	 *
	 * @returns false if there is no such animation or the stack is full.
	 */
	bool PushAnimation(AnimationId animation, eAnimationEnd on_end = eAnimationEnd::Pop, float32 speed = 1.0f);
	bool PushAnimation(const String& name, eAnimationEnd on_end = eAnimationEnd::Pop, float32 speed = 1.0f);

	void PopAnimation();
	void ClearAnimationStack();

	/// The top of the animation stack, or the rest animation if the stack is empty. False when holding the rest pose.
	bool GetActivePlayback(AnimationPlayback& out_playback) const;
	AnimationId GetActiveAnimation() const;

	/**
	 * @brief Advances the active animation by `delta_time`, poses the skeleton and uploads the pose to the bone buffer.
	 * Only runs once per frame, however many objects or passes share the skeleton.
	 */
	void Update(float32 delta_time);

	/**
	 * @brief Poses the skeleton at `time` in `animation`, or in its rest pose if there is no such animation.
	 */
	void EvaluatePose(AnimationId animation, float32 time);

	void SetExternalPose(bool enabled);

	void PoseFromDrivenBones(const Mat4f* driven_world, const uint8* is_driven);

	FX_FORCE_INLINE uint32 GetJointCount() const { return mpSkeleton != nullptr ? mpSkeleton->joint_count : 0; }

	FX_FORCE_INLINE uint32 GetPoseHash() const { return mpSkeleton->pose_hash; }
	FX_FORCE_INLINE float32 GetPoseRadius() const { return mpSkeleton->pose_radius; }
	FX_FORCE_INLINE Vec3f GetPoseCenter() const { return Vec3f(mpSkeleton->pose_center); }

	FX_FORCE_INLINE const Mat4f& GetWorldTransform(uint32 bone) const
	{
		return reinterpret_cast<const Mat4f*>(mpSkeleton->world)[bone];
	}

	FX_FORCE_INLINE const Mat4f& GetSkinningMatrix(uint32 bone) const
	{
		return reinterpret_cast<const Mat4f*>(mpSkeleton->skinning)[bone];
	}

	FX_FORCE_INLINE const Mat4f* GetSkinningMatrices() const
	{
		return reinterpret_cast<const Mat4f*>(mpSkeleton->skinning);
	}

	FX_FORCE_INLINE uint32 GetParentIndex(uint32 bone) const { return mpSkeleton->parents[bone]; }

	const char* GetBoneName(uint32 bone) const { return rx_skeleton_bone_name(mpSkeleton, bone); }
	BoneId FindBone(const char* name) const { return rx_skeleton_find_bone(mpSkeleton, name); }

public:
	uint32 BoneBufferBase = scNoBones;

	uint32 LastUpdateFrame = UINT32_MAX;

private:
	RxSkeleton* mpSkeleton = nullptr;
};

} // namespace fx
