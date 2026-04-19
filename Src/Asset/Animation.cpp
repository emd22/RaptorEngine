#include "Animation.hpp"

#include <Core/String.hpp>
#include <Math/Quat.hpp>


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
static T GetComponentAtTime(float32 time, const BoneTransformTrack<T>& track)
{
    const uint32 count = static_cast<uint32>(track.Times.Size);

    if (count == 0) {
        if constexpr (std::is_same_v<T, Quat>) {
            return Quat::sIdentity;
        }

        else if constexpr (std::is_same_v<T, Vec3f>) {
            return Vec3f::sZero;
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

void Skeleton::EvaluatePose(Animation& anim, float32 time)
{
    const uint32 joint_count = JointCount;

    for (uint32 i = 0; i < joint_count; i++) {
        const BoneTrack& track = anim.BoneTracks[i];

        Vec3f translation = GetComponentAtTime(time, track.Translation);
        Quat rotation = GetComponentAtTime(time, track.Rotation);

        LocalTransforms.pData[i] = Mat4f::AsRotation(rotation) * Mat4f::AsTranslation(translation);
    }

    for (uint32 i = 0; i < joint_count; i++) {
        const int32 parent = ParentIndices.pData[i];

        if (parent < 0) {
            WorldTransforms[i] = LocalTransforms[i];
        }
        else {
            WorldTransforms[i] = LocalTransforms[i] * WorldTransforms[parent];
        }
    }

    for (uint32 i = 0; i < joint_count; i++) {
        SkinningMatrices[i] = InvBindTransforms[i] * WorldTransforms[i];
    }
}

BoneTransform Skeleton::GetBoneTransform(const Ref<Animation>& anim, float32 time, BoneId bone_id) const
{
    BoneTransform xform {};
    if (!anim.IsValid() || bone_id == BoneNull) {
        return xform;
    }

    if (bone_id > anim->BoneTracks.Size) {
        LogError("Bone ID({}) out of range", bone_id);
        return xform;
    }

    const BoneTrack& track = anim->BoneTracks[bone_id];

    // The rotation should be taken from the matrix to have all of the parents propagated into the rotation. Currently
    // thats a pretty gnarly function to have to write, so im just going with this mess for now.
    //
    // Another strange oddity (likely due to converting from GLTF's horrid coordinates) is that Z is
    // negated. But also negating that component in the matrix causes spaghetti limbs.

    return BoneTransform(Vec3f::FlipSigns<1, 1, -1, 1>(SkinningMatrices[bone_id].GetTranslation()),
                         GetComponentAtTime(time, track.Rotation));
}

Mat4f Skeleton::GetBoneTransformMatrix(const Ref<Animation>& anim, float32 time, BoneId bone_id) const
{
    Mat4f xform {};
    if (!anim.IsValid() || bone_id == BoneNull) {
        return xform;
    }

    if (bone_id > anim->BoneTracks.Size) {
        LogError("Bone ID({}) out of range", bone_id);
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
