#pragma once

#include <Core/FxSizedArray.hpp>
#include <Core/FxString.hpp>
#include <Math/FxMat4.hpp>

namespace FxLimits {
static constexpr uint32 MaxBones = 100;
}

using FxBoneId = uint32;
static constexpr FxBoneId FxBoneNull = UINT32_MAX;

template <typename T>
struct FxAnimTrack
{
    FxSizedArray<float32> Times;
    FxSizedArray<T> Values;
};

struct FxBoneTrack
{
    FxAnimTrack<FxVec3f> Translation;
    FxAnimTrack<FxQuat> Rotation;
    FxAnimTrack<FxVec3f> Scale;
};

struct FxAnimation
{
    FxString Name;
    float32 Duration = 0.0f;
    FxSizedArray<FxBoneTrack> JointTracks;
};

struct FxSkeleton
{
public:
    void EvaluatePose(FxAnimation& anim, float32 time);

public:
    FxSizedArray<FxMat4f> InvBindTransforms;
    FxSizedArray<int32> ParentIndices;
    FxSizedArray<FxString> JointNames;
    uint32 JointCount = 0;

    FxSizedArray<FxMat4f> LocalTransforms;
    FxSizedArray<FxMat4f> WorldTransforms;
    FxSizedArray<FxMat4f> SkinningMatrices;
};
