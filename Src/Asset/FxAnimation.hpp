#pragma once

#include <Core/FxSizedArray.hpp>
#include <Core/FxString.hpp>
#include <Math/FxMat4.hpp>

using FxBoneId = uint32;
static constexpr FxBoneId FxBoneNull = UINT32_MAX;

struct FxBone
{
    FxBoneId Id = FxBoneNull;
    uint32 ParentId = FxBoneNull;
};

struct FxSkeleton
{
    FxSizedArray<FxBone> Bones;

    FxSizedArray<FxMat4f> LocalTransforms;
    FxSizedArray<FxMat4f> InvBindTransforms;
};
