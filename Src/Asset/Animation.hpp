#pragma once

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
	void EvaluatePose(Animation& anim, float32 time);
	BoneTransform GetBoneTransform(const Ref<Animation>& anim, float32 time, BoneId bone_id) const;
	Mat4f GetBoneTransformMatrix(const Ref<Animation>& anim, float32 time, BoneId bone_id) const;

	BoneId FindBone(const Ref<Animation>& anim, const String& name) const;

public:
	SizedArray<Mat4f> InvBindTransforms;
	SizedArray<BoneRestPose> RestPose;
	SizedArray<uint32> ParentIndices;
	SizedArray<String> BoneNames;
	uint32 JointCount = 0;

	SizedArray<Mat4f> LocalTransforms;
	SizedArray<Mat4f> WorldTransforms;
	SizedArray<Mat4f> SkinningMatrices;
};

} // namespace fx
