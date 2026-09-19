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
	static constexpr uint32 scNoBones = UINT32_MAX;

public:
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
	Animation* pCurrentAnimation = nullptr;
	float32 AnimationTime = 0.0f;

	/// Index of this skeleton's first matrix in `GraphicsBackend::BoneBuffer`, claimed by the last pose update.
	/// `scNoBones` if the pose did not fit into the buffer.
	uint32 BoneBufferBase = scNoBones;
	/// The frame the pose was last updated on, so that the shared skeleton only advances once per frame no matter
	/// how many objects or passes (shadow, depth prepass, forward) visit it.
	uint32 LastUpdateFrame = UINT32_MAX;
};

} // namespace fx
