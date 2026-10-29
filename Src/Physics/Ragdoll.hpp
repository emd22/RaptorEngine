#pragma once

#include "RagdollBalance.hpp"
#include "RagdollDef.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>
#include <ThirdParty/Jolt/Physics/PhysicsStepListener.h>
#include <ThirdParty/Jolt/Physics/Ragdoll/Ragdoll.h>

#include <Asset/Animation.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec3.hpp>

namespace JPH {
class BodyInterface;
class PhysicsSystem;
} // namespace JPH

namespace fx::physics {

struct BodyGroundContact;

enum class eRagdollBodyMode : uint8
{
	Kinematic,
	Powered,
	Limp,
};

class Ragdoll : public JPH::PhysicsStepListener
{
private:
	friend class RagdollBalance;
	friend class RagdollBehaviours;

	enum class eMotorOverride : uint8
	{
		None,
		Off,
		Target,
	};

	struct BodyInfo
	{
		JPH::Vec3 BoneScale = JPH::Vec3::sReplicate(1.0f);
		JPH::Vec3 ParentAnchor = JPH::Vec3::sZero();
		JPH::Vec3 LocalAxis = JPH::Vec3::sAxisY();
		JPH::Vec3 LocalCenter = JPH::Vec3::sZero();
		JPH::Vec3 BoxHalfExtent = JPH::Vec3::sZero();
		JPH::Quat BoxRotation = JPH::Quat::sIdentity();

		JPH::Vec3 HingeLocal = JPH::Vec3::sZero();
		JPH::Vec3 ParentHingeLocal = JPH::Vec3::sZero();
		JPH::Vec3 RestPosition = JPH::Vec3::sZero();
		JPH::Quat RestOrientation = JPH::Quat::sIdentity();

		int32 ParentBody = -1;

		eRagdollBodyPart Part = eRagdollBodyPart::Torso;
		eRagdollBodyMode Mode = eRagdollBodyMode::Limp;

		RagdollMuscle Muscle;

		float32 Length = 0.0f;
		float32 Radius = 0.0f;
		bool bBox = false;

		float32 Mass = 0.0f;
		float32 Inertia = 0.0f;

		float32 Strength = 1.0f;
		float32 Blend = 1.0f;
		uint32 BoneIndex = 0;

		bool bHinge = false;
		bool bAnchor = false;
		bool bFoot = false;
		bool bSnap = false;
	};

	struct BodyTarget
	{
		JPH::Vec3 Position = JPH::Vec3::sZero();
		JPH::Quat Orientation = JPH::Quat::sIdentity();
		JPH::Vec3 LinearVelocity = JPH::Vec3::sZero();
		JPH::Vec3 AngularVelocity = JPH::Vec3::sZero();
	};

	struct BodyState
	{
		JPH::Vec3 Origin = JPH::Vec3::sZero();
		JPH::Vec3 Center = JPH::Vec3::sZero();
		float32 SubtreeMass = 0.0f;
		JPH::Vec3 SubtreeMoment = JPH::Vec3::sZero();
		float32 SubtreeSecondMoment = 0.0f;
	};

	struct MotorOverride
	{
		eMotorOverride Mode = eMotorOverride::None;
		JPH::Quat Target = JPH::Quat::sIdentity();
		float32 FrequencyScale = 1.0f;
		float32 Inertia = 0.0f;
		bool bSupported = false;
		bool bNoGravityComp = false;
	};

	struct BodyLayer
	{
		JPH::Quat Pose = JPH::Quat::sIdentity();
		JPH::Quat Offset = JPH::Quat::sIdentity();
		float32 Weight = 0.0f;
		float32 GravityScale = 1.0f;
	};

public:
	Ragdoll() = default;
	Ragdoll(const Ragdoll&) = delete;
	Ragdoll& operator=(const Ragdoll&) = delete;

	bool Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix, const RagdollDef& def);

	void Activate(const Vec3f velocity);

	void Drive(const Mat4f& object_world_matrix, float32 delta_time, bool teleport = false);
	void GoLimp(const Mat4f& object_world_matrix);

	void SetBodyMode(uint32 body_index, eRagdollBodyMode mode);
	void SetAllBodyModes(eRagdollBodyMode mode);
	void SetBodyStrength(uint32 body_index, float32 strength);
	void SetBodyBlend(uint32 body_index, float32 blend);
	void SetRootAssist(float32 assist);
	void SetBalancing(bool balancing);
	void RebaseTargets();
	void ClearBodyLayers();

	int32 FindBodyIndex(JPH::BodyID body_id) const;
	JPH::BodyID GetBodyID(uint32 body_index) const;
	float32 GetBodyMass(uint32 body_index) const;
	float32 GetWorstJointError() const;
	bool GetBodyTrackingError(uint32 body_index, float32& out_distance, float32& out_angle) const;
	Vec3f GetBodyPosition(uint32 body_index) const;
	Vec3f GetBodyOrigin(uint32 body_index) const;
	Vec3f GetBodyTargetOrigin(uint32 body_index) const;
	Vec3f GetCenterOfMass() const;
	bool GetBodyGroundContact(uint32 body_index, BodyGroundContact& out_contact) const;
	eRagdollBodyPart GetBodyPart(uint32 body_index) const;

	FX_FORCE_INLINE uint32 GetBodyCount() const { return static_cast<uint32>(mBodies.Size); }
	FX_FORCE_INLINE int32 GetParentBody(uint32 body_index) const { return mBodies[body_index].ParentBody; }
	FX_FORCE_INLINE bool IsAnchor(uint32 body_index) const { return mBodies[body_index].bAnchor; }
	FX_FORCE_INLINE eRagdollBodyMode GetBodyMode(uint32 body_index) const { return mBodies[body_index].Mode; }
	FX_FORCE_INLINE float32 GetBodyStrength(uint32 body_index) const { return mBodies[body_index].Strength; }
	FX_FORCE_INLINE float32 GetRootAssist() const { return mRootAssist; }
	FX_FORCE_INLINE bool IsBalancing() const { return mbBalancing; }
	FX_FORCE_INLINE const BalanceStatus& GetBalanceStatus() const { return mBalance.GetStatus(); }

	void AddImpulse(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point);
	void WriteToSkeleton();

	void DebugDraw() const;

	bool FindBoneForBody(JPH::BodyID body_id, uint32& out_bone_index) const;

	void OnStep(const JPH::PhysicsStepListenerContext& inContext) override;

	FX_FORCE_INLINE bool IsCreated() const { return mpRagdoll != nullptr; }
	FX_FORCE_INLINE bool IsLimp() const { return mbLimp; }
	FX_FORCE_INLINE uint32 GetSerial() const { return mSerial; }

	void Destroy();

	~Ragdoll() { Destroy(); }

private:
	void RestoreTableMasses(JPH::RagdollSettings& settings);
	bool AddToPhysicsSystem(JPH::RagdollSettings& settings);
	void CreateRootAssist(const JPH::Vec3 root_position);
	void InitBodyArrays(uint32 joint_count);
	float32 WriteInitialPose();

	void MoveKinematicBodies(JPH::BodyInterface& body_interface, float32 fraction, float32 delta_time);
	void UpdateSubtrees(JPH::BodyInterface& body_interface);
	float32 GetRawSubtreeInertia(uint32 body_index, const JPH::Vec3 pivot) const;
	float32 GetSubtreeInertia(uint32 body_index, const JPH::Vec3 pivot) const;
	float32 GetSupportedInertia(uint32 body_index, const JPH::Vec3 pivot) const;
	void ApplyMotor(uint32 body_index);
	void ApplyGravityCompensation(JPH::BodyInterface& body_interface, const JPH::Vec3 gravity);
	void ApplyRootAssist(JPH::BodyInterface& body_interface, const JPH::Vec3 gravity);
	void ClearMotorOverrides();
	float32 GetMotorTorqueFraction(uint32 body_index) const;

	void SnapBodyToTarget(JPH::BodyInterface& body_interface, uint32 body_index, JPH::EActivation activation);
	Mat4f GetBodyModelMatrix(uint32 body_index) const;

	void DrawDebugBodies(SizedArray<Vec3f>& centers) const;
	void DrawDebugLinks(const SizedArray<Vec3f>& centers) const;
	void DrawDebugSupport() const;

private:
	Ref<Skeleton> mpSkeleton { nullptr };
	JPH::Ref<JPH::Ragdoll> mpRagdoll;
	JPH::PhysicsSystem* mpPhysicsSystem = nullptr;
	uint32 mSerial = 0;

	SizedArray<BodyInfo> mBodies;
	SizedArray<BodyTarget> mTargets;
	SizedArray<BodyState> mStates;
	SizedArray<MotorOverride> mMotorOverrides;
	SizedArray<BodyLayer> mLayers;
	bool mbHasTargets = false;
	bool mbHasMotors = false;
	bool mbLimp = false;

	JPH::Ref<JPH::SixDOFConstraint> mpRootAssist;
	JPH::Vec3 mRootAnchor = JPH::Vec3::sZero();
	float32 mRootAssist = 0.0f;

	RagdollBalance mBalance;
	bool mbBalancing = false;
	uint32 mContactGeneration = 0;
	uint32 mSubstep = 0;

	SizedArray<Mat4f> mDrivenWorld;
	SizedArray<uint8> mIsDriven;
	Mat4f mWorldInverse = Mat4f::scIdentity;
	bool mbPoseSettled = false;
};

} // namespace fx::physics
