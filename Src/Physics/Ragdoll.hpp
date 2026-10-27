#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/PhysicsStepListener.h>
#include <ThirdParty/Jolt/Physics/Ragdoll/Ragdoll.h>

#include <Asset/Animation.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec3.hpp>
#include <vector>

#include "RagdollDef.hpp"

namespace JPH {
class PhysicsSystem;
}

namespace fx::physics {

struct BodyGroundContact;

enum class eRagdollBodyMode : uint8
{
	Kinematic,
	Powered,
	Limp,
};

enum class eRagdollBodyPart : uint8
{
	Torso,
	Head,
	Arm,
	Leg,
};

class Ragdoll : public JPH::PhysicsStepListener
{
public:
	Ragdoll() = default;
	Ragdoll(const Ragdoll&) = delete;
	Ragdoll& operator=(const Ragdoll&) = delete;

	bool Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix, const RagdollDef& def);

	void Activate(const Vec3f velocity);

	void Drive(const Mat4f& object_world_matrix, bool teleport = false);
	void GoLimp(const Mat4f& object_world_matrix);

	void SetBodyMode(uint32 body_index, eRagdollBodyMode mode);
	void SetAllBodyModes(eRagdollBodyMode mode);
	void SetBodyStrength(uint32 body_index, float32 strength);
	void SetBodyBlend(uint32 body_index, float32 blend);

	int32 FindBodyIndex(JPH::BodyID body_id) const;
	JPH::BodyID GetBodyID(uint32 body_index) const;
	float32 GetBodyMass(uint32 body_index) const;
	float32 GetWorstJointError() const;
	bool GetBodyTrackingError(uint32 body_index, float32& out_distance, float32& out_angle) const;
	Vec3f GetBodyPosition(uint32 body_index) const;
	Vec3f GetCenterOfMass() const;
	bool GetBodyGroundContact(uint32 body_index, BodyGroundContact& out_contact) const;
	eRagdollBodyPart GetBodyPart(uint32 body_index) const;

	FX_FORCE_INLINE uint32 GetBodyCount() const { return static_cast<uint32>(mBodies.Size); }
	FX_FORCE_INLINE int32 GetParentBody(uint32 body_index) const { return mBodies[body_index].ParentBody; }
	FX_FORCE_INLINE bool IsAnchor(uint32 body_index) const { return mBodies[body_index].bAnchor; }
	FX_FORCE_INLINE eRagdollBodyMode GetBodyMode(uint32 body_index) const { return mBodies[body_index].Mode; }

	void AddImpulse(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point);

	void Destroy();

	void WriteToSkeleton();

	void DebugDraw() const;

	bool FindBoneForBody(JPH::BodyID body_id, uint32& out_bone_index) const;

	void OnStep(const JPH::PhysicsStepListenerContext& inContext) override;

	FX_FORCE_INLINE bool IsCreated() const { return mpRagdoll != nullptr; }
	FX_FORCE_INLINE bool IsLimp() const { return mbLimp; }
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
		int32 ParentBody = -1;
		bool bAnchor = false;
		eRagdollBodyMode Mode = eRagdollBodyMode::Limp;
		float32 Strength = 1.0f;
		float32 Blend = 1.0f;
		bool bSnap = false;
		float32 Mass = 0.0f;
		RagdollMuscle Muscle;
		JPH::Vec3 ParentAnchor = JPH::Vec3::sZero();
	};

	struct BodyTarget
	{
		JPH::Vec3 Position = JPH::Vec3::sZero();
		JPH::Quat Orientation = JPH::Quat::sIdentity();
		eRagdollBodyMode Mode = eRagdollBodyMode::Limp;
		float32 Strength = 1.0f;
	};

	void ApplyMotor(uint32 body_index);
	float32 GetMotorTorqueFraction(uint32 body_index) const;
	Mat4f GetBodyModelMatrix(uint32 body_index) const;

	Ref<Skeleton> mpSkeleton { nullptr };
	JPH::Ref<JPH::Ragdoll> mpRagdoll;
	JPH::PhysicsSystem* mpPhysicsSystem = nullptr;
	SizedArray<BodyInfo> mBodies;

	std::vector<BodyTarget> mTargets;
	std::vector<Mat4f> mDrivenWorld;
	std::vector<uint8> mIsDriven;

	Mat4f mWorldInverse = Mat4f::scIdentity;
	uint32 mSerial = 0;
	uint32 mSubstep = 0;
	bool mbPoseSettled = false;
	bool mbLimp = false;
	bool mbHasMotors = false;
};

} // namespace fx::physics
