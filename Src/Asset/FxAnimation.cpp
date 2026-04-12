#include "FxAnimation.hpp"

#include <Math/FxQuat.hpp>

template <typename T>
static T Interpolate(const T& a, const T& b, float32 t);

template <>
FxVec3f Interpolate(const FxVec3f& a, const FxVec3f& b, float32 t)
{
    // return b;
    return FxVec3f::Lerp(a, b, t);
}

template <>
FxQuat Interpolate(const FxQuat& a, const FxQuat& b, float32 t)
{
    // return b;
    return a.SLerp(b, t);
}

template <typename T>
static T GetTransformForTime(float32 time, const FxAnimTrack<T>& track)
{
    const uint32 count = static_cast<uint32>(track.Times.Size);

    if (count == 0) {
        if constexpr (std::is_same_v<T, FxQuat>) {
            return FxQuat::sIdentity;
        }

        else if constexpr (std::is_same_v<T, FxVec3f>) {
            return FxVec3f::sZero;
        }
        return T {};
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
        const float32 t0 = track.Times.pData[i];
        const float32 t1 = track.Times.pData[i + 1];

        if (time >= t0 && time < t1) {
            const float32 duration = t1 - t0;
            const float32 alpha = (duration > 1e-6f) ? (time - t0) / duration : 0.0f;

            return Interpolate(track.Values[i], track.Values[i + 1], alpha);
        }
    }

    return track.Values.pData[count - 1];
}

void FxSkeleton::EvaluatePose(FxAnimation& anim, float32 time)
{
    const uint32 joint_count = JointCount;

    for (uint32 i = 0; i < joint_count; i++) {
        const FxBoneTrack& track = anim.JointTracks.pData[i];

        FxVec3f translation = FxVec3f::sZero;
        FxVec3f scale = FxVec3f::sOne;
        FxQuat rotation = FxQuat::sIdentity;

        if (track.Translation.Times.Size > 0) {
            translation = GetTransformForTime(time, track.Translation);
        }

        if (track.Rotation.Times.Size > 0) {
            rotation = GetTransformForTime(time, track.Rotation);
        }


        LocalTransforms.pData[i] = FxMat4f::AsRotation(rotation) * FxMat4f::AsTranslation(translation);
    }

    for (uint32 i = 0; i < joint_count; i++) {
        const int32 parent = ParentIndices.pData[i];

        if (parent < 0) {
            WorldTransforms.pData[i] = LocalTransforms.pData[i];
        }
        else {
            WorldTransforms.pData[i] = LocalTransforms.pData[i] * WorldTransforms.pData[parent];
        }
    }

    for (uint32 i = 0; i < joint_count; i++) {
        SkinningMatrices.pData[i] = InvBindTransforms[i] * WorldTransforms[i];
    }
}
