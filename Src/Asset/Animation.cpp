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

static Vec3f QuatToScaledAngleAxis(const Quat q)
{
	float32 x = q.GetX();
	float32 y = q.GetY();
	float32 z = q.GetZ();
	float32 w = q.GetW();

	if (w < 0.0f) {
		x = -x;
		y = -y;
		z = -z;
		w = -w;
	}

	const float32 sin_half = std::sqrt(x * x + y * y + z * z);

	if (sin_half < 1e-6f) {
		return Vec3f(x, y, z) * 2.0f;
	}

	const float32 angle = 2.0f * std::atan2(sin_half, w);
	return Vec3f(x, y, z) * (angle / sin_half);
}

static Quat QuatFromScaledAngleAxis(const Vec3f v)
{
	const float32 angle = v.Length();

	if (angle < 1e-6f) {
		return Quat(v.X * 0.5f, v.Y * 0.5f, v.Z * 0.5f, 1.0f).Normalize();
	}

	const float32 half = angle * 0.5f;
	const float32 scale = std::sin(half) / angle;

	return Quat(v.X * scale, v.Y * scale, v.Z * scale, std::cos(half));
}

static Vec3f AngularVelocityToLogRate(const Vec3f log_rotation, const Vec3f angular_velocity)
{
	const float32 angle = log_rotation.Length();
	const Vec3f c1 = log_rotation.Cross(angular_velocity);
	const Vec3f c2 = log_rotation.Cross(c1);

	float32 k = 1.0f / 12.0f;

	if (angle > 1e-3f) {
		k = 1.0f / (angle * angle) - (1.0f + std::cos(angle)) / (2.0f * angle * std::sin(angle));
	}

	return angular_velocity - c1 * 0.5f + c2 * k;
}

static Vec3f RotationDelta(const Quat from, const Quat to) { return QuatToScaledAngleAxis(to * from.Conjugate()); }

static BoneDelta PoseDelta(const BoneTransform& from, const BoneTransform& to)
{
	return BoneDelta {
		.Translation = to.Translation - from.Translation,
		.Rotation = RotationDelta(from.Rotation, to.Rotation),
		.Scale = to.Scale - from.Scale,
	};
}

static void DecaySpring(Vec3f& x, Vec3f& v, float32 damping, float32 delta_time)
{
	const Vec3f j1 = v + x * damping;
	const float32 decay = std::exp(-damping * delta_time);

	x = (x + j1 * delta_time) * decay;
	v = (v - j1 * (damping * delta_time)) * decay;
}

void BlendPoses(const BoneTransform* a, const BoneTransform* b, float32 weight, BoneTransform* out, uint32 count)
{
	for (uint32 i = 0; i < count; i++) {
		Quat rotation = a[i].Rotation;
		rotation.NLerpIP(b[i].Rotation, weight);

		out[i] = BoneTransform {
			.Translation = Vec3f::Lerp(a[i].Translation, b[i].Translation, weight),
			.Rotation = rotation,
			.Scale = Vec3f::Lerp(a[i].Scale, b[i].Scale, weight),
		};
	}
}

void Skeleton::SampleLocalPose(const Animation* anim, float32 time, BoneTransform* out) const
{
	const uint32 joint_count = JointCount;

	const bool has_rest_pose = (RestPose.Size == joint_count);

	for (uint32 i = 0; i < joint_count; i++) {
		// An animation only carries channels for what it actually animates. Every component it leaves
		// out has to keep the joint's rest transform, otherwise the bone snaps to its parent's origin
		// and the mesh tears itself apart.
		const BoneTransform rest = has_rest_pose ? RestPose.pData[i] : BoneTransform {};

		if (anim == nullptr) {
			out[i] = rest;
			continue;
		}

		const BoneTrack& track = anim->BoneTracks[i];

		out[i] = BoneTransform {
			.Translation = GetComponentAtTime(time, track.Translation, rest.Translation),
			.Rotation = GetComponentAtTime(time, track.Rotation, rest.Rotation),
			.Scale = GetComponentAtTime(time, track.Scale, rest.Scale),
		};
	}
}

void Skeleton::ComposePose(const BoneTransform* pose)
{
	const uint32 joint_count = JointCount;

	for (uint32 i = 0; i < joint_count; i++) {
		LocalTransforms.pData[i] = Mat4f::AsScale(pose[i].Scale) * Mat4f::AsRotation(pose[i].Rotation) *
								   Mat4f::AsTranslation(pose[i].Translation);
	}

	const bool has_root_transforms = (RootTransforms.Size == joint_count);
	const bool has_order = (EvaluationOrder.Size == joint_count);

	for (uint32 k = 0; k < joint_count; k++) {
		const uint32 i = has_order ? EvaluationOrder.pData[k] : k;
		const uint32 parent = ParentIndices.pData[i];

		if (parent == BoneNull) {
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

void Skeleton::EvaluatePose(const Animation* anim, float32 time)
{
	EnsurePoseBuffers();

	SampleLocalPose(anim, time, mSourcePose.pData);
	ComposePose(mSourcePose.pData);
}

void Skeleton::EnsurePoseBuffers()
{
	if (mSourcePose.Size == JointCount) {
		return;
	}

	SizedArray<BoneTransform>* poses[] = { &mSourcePose, &mScratchPose, &mBlendedPose, &mOutputPoses[0],
										   &mOutputPoses[1] };

	for (SizedArray<BoneTransform>* pose : poses) {
		pose->Free();
		pose->InitSize(JointCount);
	}

	mOffsets.Free();
	mOffsets.InitSize(JointCount);
	mOffsetVelocities.Free();
	mOffsetVelocities.InitSize(JointCount);

	mOutputHistory = 0;
	mBlendDuration = 0.0f;
}

void Skeleton::BeginTransition(float32 blend_time)
{
	mPendingBlendTime = std::max(mPendingBlendTime, std::max(blend_time, 0.0f));
	mbHoldingRestPose = false;
}

void Skeleton::StartInertialization(const AnimationPlayback* playback, float32 delta_time)
{
	const float32 blend_time = mPendingBlendTime;
	mPendingBlendTime = -1.0f;

	if (blend_time <= 0.0f || mOutputHistory == 0) {
		mBlendDuration = 0.0f;
		return;
	}

	const uint32 joint_count = JointCount;
	const BoneTransform* last = mOutputPoses[mOutputIndex].pData;
	const BoneTransform* previous = mOutputPoses[mOutputIndex ^ 1].pData;
	const BoneTransform* source = mSourcePose.pData;

	float32 output_rate = 0.0f;

	if (mOutputHistory > 1 && mLastOutputDeltaTime > 0.0f) {
		output_rate = 1.0f / mLastOutputDeltaTime;
	}

	float32 source_rate = 0.0f;

	if (playback != nullptr && playback->pAnimation != nullptr && playback->Speed != 0.0f) {
		const float32 duration = playback->pAnimation->Duration;
		const float32 step = std::min(1.0f / 60.0f, duration * 0.5f);

		if (step > 0.0f) {
			float32 sample_step = step;

			if (playback->Time + step > duration) {
				sample_step = -step;
			}

			SampleLocalPose(playback->pAnimation, playback->Time + sample_step, mScratchPose.pData);
			source_rate = playback->Speed / sample_step;
		}
	}

	for (uint32 i = 0; i < joint_count; i++) {
		BoneDelta output_velocity {};
		BoneDelta source_velocity {};

		if (output_rate > 0.0f) {
			const BoneDelta delta = PoseDelta(previous[i], last[i]);
			output_velocity = BoneDelta {
				.Translation = delta.Translation * output_rate,
				.Rotation = delta.Rotation * output_rate,
				.Scale = delta.Scale * output_rate,
			};
		}

		if (source_rate != 0.0f) {
			const BoneDelta delta = PoseDelta(source[i], mScratchPose.pData[i]);
			source_velocity = BoneDelta {
				.Translation = delta.Translation * source_rate,
				.Rotation = delta.Rotation * source_rate,
				.Scale = delta.Scale * source_rate,
			};
		}

		const BoneTransform source_previous {
			.Translation = source[i].Translation - source_velocity.Translation * delta_time,
			.Rotation = (QuatFromScaledAngleAxis(source_velocity.Rotation * -delta_time) * source[i].Rotation).Normalize(),
			.Scale = source[i].Scale - source_velocity.Scale * delta_time,
		};

		const BoneDelta offset = PoseDelta(source_previous, last[i]);
		const Vec3f source_angular_velocity = source_velocity.Rotation.Rotate(QuatFromScaledAngleAxis(offset.Rotation));

		mOffsets.pData[i] = offset;
		mOffsetVelocities.pData[i] = BoneDelta {
			.Translation = output_velocity.Translation - source_velocity.Translation,
			.Rotation = AngularVelocityToLogRate(offset.Rotation, output_velocity.Rotation - source_angular_velocity),
			.Scale = output_velocity.Scale - source_velocity.Scale,
		};
	}

	mBlendDuration = blend_time;
	mBlendElapsed = 0.0f;
}

void Skeleton::ApplyInertialization(float32 delta_time)
{
	const uint32 joint_count = JointCount;
	const float32 halflife = mBlendDuration * 0.35f;
	const float32 damping = (2.0f * 0.69314718f) / halflife;

	const BoneTransform* source = mSourcePose.pData;
	BoneTransform* blended = mBlendedPose.pData;

	for (uint32 i = 0; i < joint_count; i++) {
		BoneDelta& offset = mOffsets.pData[i];
		BoneDelta& velocity = mOffsetVelocities.pData[i];

		DecaySpring(offset.Translation, velocity.Translation, damping, delta_time);
		DecaySpring(offset.Rotation, velocity.Rotation, damping, delta_time);
		DecaySpring(offset.Scale, velocity.Scale, damping, delta_time);

		blended[i] = BoneTransform {
			.Translation = source[i].Translation + offset.Translation,
			.Rotation = (QuatFromScaledAngleAxis(offset.Rotation) * source[i].Rotation).Normalize(),
			.Scale = source[i].Scale + offset.Scale,
		};
	}

	mBlendElapsed += delta_time;

	if (mBlendElapsed >= mBlendDuration * 3.0f) {
		mBlendDuration = 0.0f;
	}
}

void Skeleton::StoreOutputPose(const BoneTransform* pose, float32 delta_time)
{
	mOutputIndex ^= 1;

	BoneTransform* output = mOutputPoses[mOutputIndex].pData;

	for (uint32 i = 0; i < JointCount; i++) {
		output[i] = pose[i];
	}

	mOutputHistory = std::min(mOutputHistory + 1, 2u);
	mLastOutputDeltaTime = delta_time;
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

	mOutputHistory = 0;
	mBlendDuration = 0.0f;
	mPendingBlendTime = -1.0f;
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

void Skeleton::SetRestAnimation(const Animation* anim, float32 speed, float32 blend_time)
{
	RestPlayback = AnimationPlayback {
		.pAnimation = anim, .Time = 0.0f, .Speed = speed, .OnEnd = eAnimationEnd::Loop, .BlendTime = blend_time
	};
	mbHoldingRestPose = false;

	if (AnimationStack.Size == 0) {
		BeginTransition(blend_time);
	}
}

bool Skeleton::PushAnimation(const Animation* anim, eAnimationEnd on_end, float32 speed, float32 blend_time)
{
	if (anim == nullptr) {
		return false;
	}

	if (AnimationStack.Size >= AnimationStack.Capacity) {
		LogWarning(LC_ASSET, "Animation stack is full ({}), not playing '{}'", AnimationStack.Capacity, anim->Name);
		return false;
	}

	AnimationStack.Insert(AnimationPlayback {
		.pAnimation = anim, .Time = 0.0f, .Speed = speed, .OnEnd = on_end, .BlendTime = blend_time });
	BeginTransition(blend_time);

	return true;
}

bool Skeleton::PushAnimation(const String& name, eAnimationEnd on_end, float32 speed, float32 blend_time)
{
	const Animation* anim = FindAnimation(name);

	if (anim == nullptr) {
		LogWarning(LC_ASSET, "Could not find animation '{}'", name);
		return false;
	}

	return PushAnimation(anim, on_end, speed, blend_time);
}

void Skeleton::PopAnimation(float32 blend_time)
{
	if (AnimationStack.Size > 0) {
		--AnimationStack.Size;
		BeginTransition(blend_time);
	}
}

void Skeleton::ClearAnimationStack(float32 blend_time)
{
	if (AnimationStack.Size > 0) {
		AnimationStack.Clear();
		BeginTransition(blend_time);
	}
}

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

	if (mbExternalPose) {
		return;
	}

	AnimationPlayback* playback = GetActivePlayback();
	const bool has_pending_transition = (mPendingBlendTime >= 0.0f);

	if (playback == nullptr && mbHoldingRestPose && !has_pending_transition && !IsTransitioning()) {
		mOutputHistory = std::min(mOutputHistory, 1u);
		return;
	}

	EnsurePoseBuffers();

	const Animation* anim = nullptr;
	float32 time = 0.0f;

	if (playback != nullptr) {
		anim = playback->pAnimation;
		time = playback->Time;
	}

	SampleLocalPose(anim, time, mSourcePose.pData);

	if (has_pending_transition) {
		StartInertialization(playback, delta_time);
	}

	const BoneTransform* output = mSourcePose.pData;

	if (IsTransitioning()) {
		ApplyInertialization(delta_time);
		output = mBlendedPose.pData;
	}

	StoreOutputPose(output, delta_time);
	ComposePose(output);

	if (playback == nullptr) {
		mbHoldingRestPose = !IsTransitioning();
		return;
	}

	// Advanced after posing so a freshly pushed animation shows its first frame
	const float32 duration = playback->pAnimation->Duration;
	playback->Time += delta_time * playback->Speed;

	if (playback->Time >= duration) {
		// The rest animation is not on the stack, so it can only ever loop
		const eAnimationEnd on_end = (playback == &RestPlayback) ? eAnimationEnd::Loop : playback->OnEnd;

		switch (on_end) {
		case eAnimationEnd::Pop:
			PopAnimation(playback->BlendTime);
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
