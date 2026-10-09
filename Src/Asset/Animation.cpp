#include "Animation.hpp"

#include <Core/String.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <World.hpp>
#include <algorithm>
#include <cmath>
#include <vector>


namespace fx {

template <typename T>
static T Interpolate(const T a, const T b, float32 t);

template <>
Vec3f Interpolate(const Vec3f a, const Vec3f b, float32 t)
{
	// return b;
	return Vec3f::Lerp(a, b, t);
}

template <>
Quat Interpolate(const Quat a, const Quat b, float32 t)
{
	// return b;
	return a.SLerp(b, t);
}

template <typename T>
static T Hermite(const T& p0, const T& out0, const T& p1, const T& in1, float32 t, float32 dt);

template <>
Vec3f Hermite(const Vec3f& p0, const Vec3f& out0, const Vec3f& p1, const Vec3f& in1, float32 t, float32 dt)
{
	const float32 t2 = t * t;
	const float32 t3 = t2 * t;

	return p0 * (2.0f * t3 - 3.0f * t2 + 1.0f) + out0 * (dt * (t3 - 2.0f * t2 + t)) + p1 * (-2.0f * t3 + 3.0f * t2) +
		   in1 * (dt * (t3 - t2));
}

template <>
Quat Hermite(const Quat& p0, const Quat& out0, const Quat& p1, const Quat& in1, float32 t, float32 dt)
{
	const float32 t2 = t * t;
	const float32 t3 = t2 * t;

	const float32 a = 2.0f * t3 - 3.0f * t2 + 1.0f;
	const float32 b = dt * (t3 - 2.0f * t2 + t);
	const float32 c = -2.0f * t3 + 3.0f * t2;
	const float32 d = dt * (t3 - t2);

	const float32 dot = p0.GetX() * p1.GetX() + p0.GetY() * p1.GetY() + p0.GetZ() * p1.GetZ() + p0.GetW() * p1.GetW();
	const float32 sign = (dot < 0.0f) ? -1.0f : 1.0f;

	return Quat(a * p0.GetX() + b * out0.GetX() + sign * (c * p1.GetX() + d * in1.GetX()),
				a * p0.GetY() + b * out0.GetY() + sign * (c * p1.GetY() + d * in1.GetY()),
				a * p0.GetZ() + b * out0.GetZ() + sign * (c * p1.GetZ() + d * in1.GetZ()),
				a * p0.GetW() + b * out0.GetW() + sign * (c * p1.GetW() + d * in1.GetW()))
		.Normalize();
}

template <typename T>
static T GetComponentAtTime(float32 time, const BoneTransformTrack<T>& track, const T& empty_default)
{
	const uint32 count = static_cast<uint32>(track.Times.Size);

	if (count == 0 || track.Values.Size < count) {
		return empty_default;
	}

	if (time <= track.Times.pData[0]) {
		return track.Values.pData[0];
	}

	if (time >= track.Times.pData[count - 1]) {
		return track.Values.pData[count - 1];
	}

	const float32* times = track.Times.pData;

	const uint32 next = static_cast<uint32>(std::upper_bound(times, times + count, time) - times);
	const uint32 current = next - 1;

	if (track.Interpolation == eAnimationInterpolation::Step) {
		return track.Values.pData[current];
	}

	const float32 t0 = times[current];
	const float32 t1 = times[next];

	const float32 duration = t1 - t0;
	const float32 alpha = (duration > 1e-6f) ? (time - t0) / duration : 0.0f;

	if (track.Interpolation == eAnimationInterpolation::CubicSpline && track.InTangents.Size >= count &&
		track.OutTangents.Size >= count) {
		return Hermite(track.Values.pData[current], track.OutTangents.pData[current], track.Values.pData[next],
					   track.InTangents.pData[next], alpha, duration);
	}

	return Interpolate(track.Values.pData[current], track.Values.pData[next], alpha);
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
	const bool has_order = (EvaluationOrder.Size == joint_count);

	for (uint32 k = 0; k < joint_count; k++) {
		const uint32 i = has_order ? EvaluationOrder.pData[k] : k;
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

	UpdatePoseSummary();
}

void Skeleton::UpdatePoseSummary()
{
	if (JointCount == 0 || SkinningMatrices.pData == nullptr) {
		return;
	}

	uint32 hash = 2166136261u;

	const uint32* words = reinterpret_cast<const uint32*>(SkinningMatrices.pData);
	const size_t word_count = static_cast<size_t>(JointCount) * 16;

	for (size_t i = 0; i < word_count; i++) {
		hash = (hash ^ words[i]) * 16777619u;
	}

	PoseHash = hash;

	Vec3f min = Vec3f(1.0e30f, 1.0e30f, 1.0e30f);
	Vec3f max = Vec3f(-1.0e30f, -1.0e30f, -1.0e30f);

	for (uint32 i = 0; i < JointCount; i++) {
		const float32* row = &WorldTransforms[i].RawData[12];

		min = Vec3f(std::min(min.X, row[0]), std::min(min.Y, row[1]), std::min(min.Z, row[2]));
		max = Vec3f(std::max(max.X, row[0]), std::max(max.Y, row[1]), std::max(max.Z, row[2]));
	}

	PoseCenter = (min + max) * 0.5f;
	PoseRadius = (max - min).Length() * 0.5f;
}

void Skeleton::SetExternalPose(bool enabled)
{
	mbExternalPose = enabled;
	mbHoldingRestPose = false;
}

void Skeleton::PoseFromDrivenBones(const Mat4f* driven_world, const uint8* is_driven)
{
	const bool has_root_transforms = (RootTransforms.Size == JointCount);
	const bool has_order = (EvaluationOrder.Size == JointCount);

	for (uint32 k = 0; k < JointCount; k++) {
		const uint32 i = has_order ? EvaluationOrder.pData[k] : k;
		const uint32 parent = ParentIndices.pData[i];

		if (is_driven[i]) {
			WorldTransforms[i] = driven_world[i];
		}
		else if (parent == BoneNull) {
			WorldTransforms[i] = has_root_transforms ? (LocalTransforms[i] * RootTransforms[i]) : LocalTransforms[i];
		}
		else {
			WorldTransforms[i] = LocalTransforms[i] * WorldTransforms[parent];
		}

		SkinningMatrices[i] = InvBindTransforms[i] * WorldTransforms[i];
	}

	mbHoldingRestPose = false;

	UpdatePoseSummary();
}

template <typename T>
static SizedArray<T> MakeView(SizedArray<T>& source)
{
	return SizedArray<T>(source.pData, source.Size);
}

Ref<Skeleton> Skeleton::CreateInstance(const Ref<Skeleton>& source)
{
	if (!source.IsValid()) {
		return Ref<Skeleton>(nullptr);
	}

	Ref<Skeleton> base = source->pSource.IsValid() ? source->pSource : source;
	Ref<Skeleton> instance = Ref<Skeleton>::New();

	Skeleton& inst = *instance;
	Skeleton& src = *base;

	inst.pSource = base;
	inst.JointCount = src.JointCount;

	inst.InvBindTransforms = MakeView(src.InvBindTransforms);
	inst.RestPose = MakeView(src.RestPose);
	inst.RootTransforms = MakeView(src.RootTransforms);
	inst.ParentIndices = MakeView(src.ParentIndices);
	inst.EvaluationOrder = MakeView(src.EvaluationOrder);
	inst.BoneNames = MakeView(src.BoneNames);
	inst.Animations = MakeView(src.Animations);

	inst.LocalTransforms.InitSize(src.JointCount);
	inst.WorldTransforms.InitSize(src.JointCount);
	inst.SkinningMatrices.InitSize(src.JointCount);

	inst.RestPlayback = src.RestPlayback;
	inst.RestPlayback.Time = 0.0f;

	if (inst.JointCount > 0) {
		inst.EvaluatePose(inst.RestPlayback.pAnimation, 0.0f);
	}

	return instance;
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

	AdvancePose(delta_time);

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

void Skeleton::AdvancePose(float32 delta_time)
{
	const uint32 current_frame = renderer::gGraphics->GetElapsedFrameCount();

	if (LastPoseFrame == current_frame) {
		return;
	}

	LastPoseFrame = current_frame;

	AnimationPlayback* playback = mbExternalPose ? nullptr : GetActivePlayback();

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
	else if (!mbExternalPose && !mbHoldingRestPose) {
		EvaluatePose(nullptr, 0.0f);
		mbHoldingRestPose = true;
	}
}

void Skeleton::BuildEvaluationOrder()
{
	const uint32 joint_count = JointCount;

	EvaluationOrder.Free();
	EvaluationOrder.InitCapacity(joint_count);

	std::vector<uint8> state(joint_count, 0);
	std::vector<uint32> chain;

	for (uint32 joint = 0; joint < joint_count; joint++) {
		chain.clear();

		uint32 current = joint;

		while (current < joint_count && state[current] == 0) {
			state[current] = 1;
			chain.push_back(current);

			current = (ParentIndices.Size == joint_count) ? ParentIndices.pData[current] : BoneNull;
		}

		for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
			state[*it] = 2;
			EvaluationOrder.Insert(*it);
		}
	}
}

} // namespace fx
