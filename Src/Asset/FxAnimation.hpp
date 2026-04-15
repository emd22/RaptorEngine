#pragma once

#include <Core/FxRef.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxString.hpp>
#include <Math/FxMat4.hpp>
#include <Math/FxQuat.hpp>
#include <Math/FxVec3.hpp>

namespace FxLimits {
static constexpr uint32 MaxBones = 100;
}

using FxBoneId = uint32;
static constexpr FxBoneId FxBoneNull = UINT32_MAX;

template <typename T>
struct FxBoneTransformTrack
{
    FxSizedArray<float32> Times;
    FxSizedArray<T> Values;
};

struct FxBoneTrack
{
    FxBoneTransformTrack<FxVec3f> Translation;
    FxBoneTransformTrack<FxQuat> Rotation;
};

struct FxAnimation
{
    FxString Name;
    float32 Duration = 0.0f;
    FxSizedArray<FxBoneTrack> BoneTracks;
};

struct FxBoneTransform
{
    FxBoneTransform() = default;
    FxBoneTransform(const FxVec3f& position, const FxQuat& rotation) : Position(position), Rotation(rotation) {}

    FxVec3f Position = FxVec3f::sZero;
    FxQuat Rotation = FxQuat::sIdentity;
};

struct FxSkeleton
{
public:
    void EvaluatePose(FxAnimation& anim, float32 time);
    FxBoneTransform GetBoneTransform(const FxRef<FxAnimation>& anim, float32 time, FxBoneId bone_id) const;

    FxBoneId FindBone(const FxRef<FxAnimation>& anim, const FxString& name) const;

public:
    FxSizedArray<FxMat4f> InvBindTransforms;
    FxSizedArray<uint32> ParentIndices;
    FxSizedArray<FxString> BoneNames;
    uint32 JointCount = 0;

    FxSizedArray<FxMat4f> LocalTransforms;
    FxSizedArray<FxMat4f> WorldTransforms;
    FxSizedArray<FxMat4f> SkinningMatrices;
};
