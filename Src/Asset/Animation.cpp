#include "Animation.hpp"

#include <Core/String.hpp>
#include <Math/Quat.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <World.hpp>

#include <cmath>


namespace fx {

template <typename T>
static T Interpolate(const T& a, const T& b, float32 t);

template <>
Vec3f Interpolate(const Vec3f& a, const Vec3f& b, float32 t)
{
	// return b;
	return Vec3f::Lerp(a, b, t);
}

template <>
Quat Interpolate(const Quat& a, const Quat& b, float32 t)
{
	// return b;
	return a.SLerp(b, t);
}

template <typename T>
static T GetComponentAtTime(float32 time, const BoneTransformTrack<T>& track, const T& empty_default)
{
	const uint32 count = static_cast<uint32>(track.Times.Size);

	if (count == 0) {
		return empty_default;
	}

	// Clamp to ends
	if (time <= track.Times.pData[0]) {
		return track.Values.pData[0];
	}

	if (time >= track.Times.pData[count - 1]) {
		return track.Values.pData[count - 1];
	}

	// Find bracketing keyframes
	for (uint32 i = 0; i < count - 1; i++) {
		const float32 t0 = track.Times[i];
		const float32 t1 = track.Times[i + 1];

		if (time >= t0 && time < t1) {
			const float32 duration = t1 - t0;
			const float32 alpha = (duration > 1e-6f) ? (time - t0) / duration : 0.0f;

			return Interpolate(track.Values[i], track.Values[i + 1], alpha);
		}
	}

	return track.Values.pData[count - 1];
}

void Skeleton::EvaluatePose(const Animation* anim, float32 time)
{
	const uint32 joint_count = JointCount;

	const bool has_rest_pose = (RestPose.Size == joint_count);

	for (uint32 i = 0; i < joint_count; i++) {
		// An animation only carries channels for what it actually animates. Every component it leaves
		// out has to keep the joint's rest transform, otherwise the bone snaps to its parent's origin
		// and the mesh tears itself apart.
		const BoneRestPose rest = has_rest_pose ? RestPose.pData[i] : BoneRestPose {};

		Vec3f translation = rest.Translation;
		Quat rotation = rest.Rotation;
		Vec3f scale = rest.Scale;

		if (anim != nullptr) {
			const BoneTrack& track = anim->BoneTracks[i];

			translation = GetComponentAtTime(time, track.Translation, rest.Translation);
			rotation = GetComponentAtTime(time, track.Rotation, rest.Rotation);
			scale = GetComponentAtTime(time, track.Scale, rest.Scale);
		}

		LocalTransforms.pData[i] = Mat4f::AsScale(scale) * Mat4f::AsRotation(rotation) *
								   Mat4f::AsTranslation(translation);
	}

	const bool has_root_transforms = (RootTransforms.Size == joint_count);

	for (uint32 i = 0; i < joint_count; i++) {
		const int32 parent = ParentIndices.pData[i];

		if (parent < 0) {
			// A root joint's parent chain lives outside the skin, so its transform has to come from the
			// node hierarchy instead of from another joint.
			WorldTransforms[i] = has_root_transforms ? (LocalTransforms[i] * RootTransforms[i]) : LocalTransforms[i];
		}
		else {
			WorldTransforms[i] = LocalTransforms[i] * WorldTransforms[parent];
		}
	}

	for (uint32 i = 0; i < joint_count; i++) {
		SkinningMatrices[i] = InvBindTransforms[i] * WorldTransforms[i];
	}
}

const Animation* Skeleton::FindAnimation(const String& name) const
{
	for (const Animation& anim : Animations) {
		if (anim.Name == name) {
			return &anim;
		}
	}

	return nullptr;
}

void Skeleton::SetRestAnimation(const Animation* anim, float32 speed)
{
	RestPlayback = AnimationPlayback { .pAnimation = anim, .Time = 0.0f, .Speed = speed, .OnEnd = eAnimationEnd::Loop };
	mbHoldingRestPose = false;
}

bool Skeleton::PushAnimation(const Animation* anim, eAnimationEnd on_end, float32 speed)
{
	if (anim == nullptr) {
		return false;
	}

	if (AnimationStack.Size >= AnimationStack.Capacity) {
		LogWarning(LC_ASSET, "Animation stack is full ({}), not playing '{}'", AnimationStack.Capacity, anim->Name);
		return false;
	}

	AnimationStack.Insert(AnimationPlayback { .pAnimation = anim, .Time = 0.0f, .Speed = speed, .OnEnd = on_end });
	mbHoldingRestPose = false;

	return true;
}

bool Skeleton::PushAnimation(const String& name, eAnimationEnd on_end, float32 speed)
{
	const Animation* anim = FindAnimation(name);

	if (anim == nullptr) {
		LogWarning(LC_ASSET, "Could not find animation '{}'", name);
		return false;
	}

	return PushAnimation(anim, on_end, speed);
}

void Skeleton::PopAnimation()
{
	if (AnimationStack.Size > 0) {
		--AnimationStack.Size;
	}
}

void Skeleton::ClearAnimationStack() { AnimationStack.Clear(); }

AnimationPlayback* Skeleton::GetActivePlayback()
{
	if (AnimationStack.Size > 0) {
		return &AnimationStack[AnimationStack.Size - 1];
	}

	if (RestPlayback.pAnimation != nullptr) {
		return &RestPlayback;
	}

	return nullptr;
}

const Animation* Skeleton::GetActiveAnimation()
{
	const AnimationPlayback* playback = GetActivePlayback();
	return (playback != nullptr) ? playback->pAnimation : nullptr;
}

void Skeleton::Update(float32 delta_time)
{
	const uint32 current_frame = renderer::gGraphics->GetElapsedFrameCount();

	if (LastUpdateFrame == current_frame) {
		return;
	}

	LastUpdateFrame = current_frame;

	AnimationPlayback* playback = GetActivePlayback();

	if (playback != nullptr) {
		EvaluatePose(playback->pAnimation, playback->Time);

		// Advanced after posing so a freshly pushed animation shows its first frame
		const float32 duration = playback->pAnimation->Duration;
		playback->Time += delta_time * playback->Speed;

		if (playback->Time >= duration) {
			// The rest animation is not on the stack, so it can only ever loop
			const eAnimationEnd on_end = (playback == &RestPlayback) ? eAnimationEnd::Loop : playback->OnEnd;

			switch (on_end) {
			case eAnimationEnd::Pop:
				PopAnimation();
				break;
			case eAnimationEnd::Hold:
				playback->Time = duration;
				break;
			case eAnimationEnd::Loop:
				playback->Time = (duration > 0.0f) ? std::fmod(playback->Time, duration) : 0.0f;
				break;
			}
		}
	}
	else if (!mbHoldingRestPose) {
		EvaluatePose(nullptr, 0.0f);
		mbHoldingRestPose = true;
	}

	// The bone buffer is rewritten every frame, so the pose is uploaded every frame even when it did not change
	BoneBufferBase = renderer::gGraphics->BoneBuffer.CopySlots(SkinningMatrices.pData, SkinningMatrices.Size);

	if (BoneBufferBase == Skeleton::scNoBones) {
		static bool sbWarned = false;

		if (!sbWarned) {
			LogWarning(LC_RENDER, "Bone buffer is full ({} matrices), ({} bones) will not be drawn",
					   Limits::MaxBoneMatrices, SkinningMatrices.Size);
			sbWarned = true;
		}
	}
}

BoneTransform Skeleton::GetBoneTransform(const Ref<Animation>& anim, float32 time, BoneId bone_id) const
{
	BoneTransform xform {};
	if (!anim.IsValid() || bone_id == BoneNull) {
		return xform;
	}

	if (bone_id >= anim->BoneTracks.Size) {
		LogError(LC_ASSET, "Bone ID({}) out of range", bone_id);
		return xform;
	}

	const BoneTrack& track = anim->BoneTracks[bone_id];

	// The rotation should be taken from the matrix to have all of the parents propagated into the rotation. Currently
	// thats a pretty gnarly function to have to write, so im just going with this mess for now.
	//
	// Another strange oddity (likely due to converting from GLTF's horrid coordinates) is that Z is
	// negated. But also negating that component in the matrix causes spaghetti limbs.

	return BoneTransform(Vec3f::FlipSigns<1, 1, -1, 1>(SkinningMatrices[bone_id].GetTranslation()),
						 GetComponentAtTime(time, track.Rotation, Quat::scIdentity));
}

Mat4f Skeleton::GetBoneTransformMatrix(const Ref<Animation>& anim, float32 time, BoneId bone_id) const
{
	Mat4f xform {};
	if (!anim.IsValid() || bone_id == BoneNull) {
		return xform;
	}

	if (bone_id >= anim->BoneTracks.Size) {
		LogError(LC_ASSET, "Bone ID({}) out of range", bone_id);
		return xform;
	}

	const BoneTrack& track = anim->BoneTracks[bone_id];
	return SkinningMatrices[bone_id];
}


BoneId Skeleton::FindBone(const Ref<Animation>& anim, const String& name) const
{
	if (!anim.IsValid()) {
		return BoneNull;
	}

	for (int32 index = 0; index < BoneNames.Size; index++) {
		if (BoneNames[index] == name) {
			return index;
			break;
		}
	}

	return BoneNull;
}

} // namespace fx
