#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Ragdoll/Ragdoll.h>

#include <Asset/Animation.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec3.hpp>
#include <vector>

namespace fx::physics {

class Ragdoll
{
public:
	Ragdoll() = default;
	Ragdoll(const Ragdoll&) = delete;
	Ragdoll& operator=(const Ragdoll&) = delete;

	bool Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix);

	void Activate(const Vec3f& velocity);

	void Destroy();

	void WriteToSkeleton();

	void DebugDraw() const;

	FX_FORCE_INLINE bool IsCreated() const { return mpRagdoll != nullptr; }
	FX_FORCE_INLINE uint32 GetSerial() const { return mSerial; }

	~Ragdoll() { Destroy(); }

private:
	struct BodyInfo
	{
		uint32 BoneIndex = 0;
		JPH::Vec3 LocalAxis = JPH::Vec3::sAxisY();
		JPH::Vec3 LocalCenter = JPH::Vec3::sZero();
		float32 Length = 0.0f;
		float32 Radius = 0.0f;
	};

	Ref<Skeleton> mpSkeleton { nullptr };
	JPH::Ref<JPH::Ragdoll> mpRagdoll;
	SizedArray<BodyInfo> mBodies;

	std::vector<Mat4f> mDrivenWorld;
	std::vector<uint8> mIsDriven;

	Mat4f mWorldInverse = Mat4f::scIdentity;
	uint32 mSerial = 0;
	bool mbPoseSettled = false;
};

} // namespace fx::physics
