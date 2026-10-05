#include "Animation.hpp"

#include <Core/String.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <World.hpp>

namespace fx {

static_assert(Skeleton::scNoBones == RX_NO_BONE);
static_assert(NoAnimation == RX_NO_ANIMATION);
static_assert(sizeof(RxSkeleton) == 64);
static_assert(sizeof(RxPlayback) == 16);
static_assert(alignof(Mat4f) == 16);

Skeleton::Skeleton()
{
	AssertEqual(rx_skeleton_max_animation_stack(), scMaxAnimationStack);

	mpSkeleton = rx_skeleton_new(0, nullptr, nullptr, nullptr, nullptr, nullptr);
}

Skeleton::~Skeleton()
{
	rx_skeleton_free(mpSkeleton);
	mpSkeleton = nullptr;
}

Ref<Skeleton> Skeleton::CreateInstance(const Ref<Skeleton>& source)
{
	if (!source.IsValid() || source->mpSkeleton == nullptr) {
		return Ref<Skeleton>(nullptr);
	}

	Ref<Skeleton> instance = Ref<Skeleton>::New();
	instance->mpSkeleton = rx_skeleton_create_instance(source->mpSkeleton);

	return instance;
}

void Skeleton::Adopt(RxSkeleton* skeleton)
{
	rx_skeleton_free(mpSkeleton);
	mpSkeleton = skeleton;
}

AnimationId Skeleton::FindAnimation(const String& name) const
{
	return rx_skeleton_find_animation(mpSkeleton, name.CStr());
}

void Skeleton::SetRestAnimation(AnimationId animation, float32 speed)
{
	rx_skeleton_set_rest_animation(mpSkeleton, animation, speed);
}

bool Skeleton::PushAnimation(AnimationId animation, eAnimationEnd on_end, float32 speed)
{
	if (animation == NoAnimation) {
		return false;
	}

	if (rx_skeleton_stack_is_full(mpSkeleton)) {
		LogWarning(LC_ASSET, "Animation stack is full ({}), not playing animation {}", scMaxAnimationStack, animation);
		return false;
	}

	return rx_skeleton_push_animation(mpSkeleton, animation, static_cast<uint8>(on_end), speed) != 0;
}

bool Skeleton::PushAnimation(const String& name, eAnimationEnd on_end, float32 speed)
{
	const AnimationId animation = FindAnimation(name);

	if (animation == NoAnimation) {
		LogWarning(LC_ASSET, "Could not find animation '{}'", name);
		return false;
	}

	return PushAnimation(animation, on_end, speed);
}

void Skeleton::PopAnimation() { rx_skeleton_pop_animation(mpSkeleton); }

void Skeleton::ClearAnimationStack() { rx_skeleton_clear_animation_stack(mpSkeleton); }

bool Skeleton::GetActivePlayback(AnimationPlayback& out_playback) const
{
	RxPlayback playback {};

	if (!rx_skeleton_active_playback(mpSkeleton, &playback)) {
		return false;
	}

	out_playback = AnimationPlayback { .Animation = playback.animation,
									   .Time = playback.time,
									   .Speed = playback.speed,
									   .OnEnd = static_cast<eAnimationEnd>(playback.on_end) };

	return true;
}

AnimationId Skeleton::GetActiveAnimation() const
{
	AnimationPlayback playback;

	return GetActivePlayback(playback) ? playback.Animation : NoAnimation;
}

void Skeleton::EvaluatePose(AnimationId animation, float32 time)
{
	rx_skeleton_evaluate_pose(mpSkeleton, animation, time);
}

void Skeleton::SetExternalPose(bool enabled) { rx_skeleton_set_external_pose(mpSkeleton, enabled ? 1 : 0); }

void Skeleton::PoseFromDrivenBones(const Mat4f* driven_world, const uint8* is_driven)
{
	rx_skeleton_pose_from_driven_bones(mpSkeleton, reinterpret_cast<const float32*>(driven_world), is_driven);
}

void Skeleton::Update(float32 delta_time)
{
	const uint32 current_frame = renderer::gGraphics->GetElapsedFrameCount();

	if (LastUpdateFrame == current_frame) {
		return;
	}

	LastUpdateFrame = current_frame;

	rx_skeleton_advance(mpSkeleton, delta_time);

	const uint32 joint_count = GetJointCount();

	BoneBufferBase = renderer::gGraphics->BoneBuffer.CopySlots(GetSkinningMatrices(), joint_count);

	if (BoneBufferBase == Skeleton::scNoBones) {
		static bool sbWarned = false;

		if (!sbWarned) {
			LogWarning(LC_RENDER, "Bone buffer is full ({} matrices), ({} bones) will not be drawn",
					   Limits::MaxBoneMatrices, joint_count);
			sbWarned = true;
		}
	}
}

} // namespace fx
