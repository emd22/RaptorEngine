#pragma once

#include "RagdollBalance.hpp"
#include "RagdollDef.hpp"
#include "RagdollRig.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Collision/GroupFilterTable.h>
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
class SwingTwistConstraint;
} // namespace JPH

namespace fx::physics {

struct BodyGroundContact;

enum class eRagdollBodyMode : uint8
{
	Kinematic,
	Powered,
	Limp,
};

enum class eRagdollShapeCollider
{
	/// The default collider for sections of a ragdoll
	Capsule,
	/// Box, used for surfaces meant to be stable (e.g. feet)
	Box,
};


struct RDSegmentInfo
{
	/// Row lengths of the bone's model matrix (0.01 on centimetre rigs), stripped for Jolt and put back when posing
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

	eRagdollShapeCollider ColliderType = eRagdollShapeCollider::Capsule;

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

struct RDSegmentTarget
{
	JPH::Vec3 Position = JPH::Vec3::sZero();
	JPH::Quat Orientation = JPH::Quat::sIdentity();
	JPH::Vec3 LinearVelocity = JPH::Vec3::sZero();
	JPH::Vec3 AngularVelocity = JPH::Vec3::sZero();
};

struct RDSegmentState
{
	JPH::Vec3 Origin = JPH::Vec3::sZero();
	JPH::Vec3 Center = JPH::Vec3::sZero();
	JPH::Vec3 Velocity = JPH::Vec3::sZero();
	JPH::Vec3 AngularVelocity = JPH::Vec3::sZero();
	JPH::Quat Orientation = JPH::Quat::sIdentity();

	float32 SubtreeMass = 0.0f;
	JPH::Vec3 SubtreeMoment = JPH::Vec3::sZero();
	float32 SubtreeSecondMoment = 0.0f;
};


class Ragdoll : public JPH::PhysicsStepListener
{
private:
	/// The balance controller is part of the motor system and writes targets and motor overrides directly
	friend class RagdollBalance;

	enum class eMotorOverride : uint8
	{
		None,
		Off,
		Target,
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

public:
	/// Pose a behaviour layer blends over the animated target, applied relative to the parent body
	struct BodyLayer
	{
		JPH::Quat Pose = JPH::Quat::sIdentity();
		JPH::Quat Offset = JPH::Quat::sIdentity();
		float32 Weight = 0.0f;
		float32 GravityScale = 1.0f;
	};


private:
	/// A body's pose before the last physics update, so the drawn pose can sit between the last two steps
	struct BodyPose
	{
		JPH::Vec3 Position = JPH::Vec3::sZero();
		JPH::Quat Orientation = JPH::Quat::sIdentity();
		bool bValid = false;
	};

public:
	Ragdoll() = default;
	Ragdoll(const Ragdoll&) = delete;
	Ragdoll& operator=(const Ragdoll&) = delete;

	bool Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix, const RagdollDef& def);

	static JPH::ObjectLayer GetModeLayer(eRagdollBodyMode mode);

	void Activate(const Vec3f velocity);

	void Drive(const Mat4f& object_world_matrix, float32 delta_time, bool teleport = false);
	void GoLimp(const Mat4f& object_world_matrix);

	void SetBodyMode(uint32 body_index, eRagdollBodyMode mode);
	void SetAllBodyModes(eRagdollBodyMode mode);
	void PlaceFeetOnGround();
	void SetBodyStrength(uint32 body_index, float32 strength);
	void SetBodyBlend(uint32 body_index, float32 blend);
	void SetAllBodyBlends(float32 blend);
	void SetRootAssist(float32 assist);
	void SetBalancing(bool balancing);

	FX_FORCE_INLINE void SetBalanceAssist(const BalanceAssist& assist) { mBalance.SetAssist(assist); }
	FX_FORCE_INLINE void SetStaggerVelocity(const Vec3f velocity)
	{
		mBalance.SetStaggerVelocity(JPH::Vec3(velocity.X, velocity.Y, velocity.Z));
	}
	FX_FORCE_INLINE Vec3f GetStaggerVelocity() const
	{
		const JPH::Vec3 velocity = mBalance.GetStaggerVelocity();
		return Vec3f(velocity.GetX(), velocity.GetY(), velocity.GetZ());
	}
	void RebaseTargets();

	void ClearBodyLayers();
	void SetBodyLayers(const SizedArray<BodyLayer>& layers, float32 gravity_scale);

	int32 FindSegmentIndex(JPH::BodyID body_id) const;
	FX_FORCE_INLINE JPH::BodyID GetBodyID(uint32 body_index) const
	{
		return mpRagdoll->GetBodyID(static_cast<int>(body_index));
	}

	float32 GetBodyMass(uint32 body_index) const;
	float32 GetWorstJointError() const;
	bool GetBodyTrackingError(uint32 body_index, float32& out_distance, float32& out_angle) const;
	Vec3f GetBodyPosition(uint32 body_index) const;
	Vec3f GetBodyOrigin(uint32 body_index) const;
	Vec3f GetBodyTargetOrigin(uint32 body_index) const;
	/// Horizontal direction the pelvis faces, as simulated or as last animated
	Vec3f GetPelvisHeading(bool animated) const;
	bool GetBodyGroundContact(uint32 body_index, BodyGroundContact& out_contact) const;
	bool IsBodyOnGround(uint32 body_index, const JPH::Vec3 up, float32 min_up) const;
	static bool IsSupportNormal(const BodyGroundContact& contact, const JPH::Vec3 up, float32 min_up);
	eRagdollBodyPart GetBodyPart(uint32 body_index) const;

	void SampleSegment(JPH::BodyInterface& body_interface, uint32 body_index, RDSegmentState& out_state) const;
	bool GetMassAverage(const SizedArray<RDSegmentState>& states, JPH::Vec3& out_center, JPH::Vec3& out_velocity) const;
	void GetHingeChainRotations(uint32 upper, uint32 lower, const JPH::Vec3 upper_direction,
								const JPH::Vec3 lower_direction, const JPH::Vec3 hinge, JPH::Quat& out_upper,
								JPH::Quat& out_lower) const;

	/**
	 * @brief Returns the number of segments in a ragdoll body.
	 */
	FX_FORCE_INLINE uint32 GetSegmentCount() const { return static_cast<uint32>(mSegments.Size); }

	FX_FORCE_INLINE float32 GetTotalMass() const { return mTotalMass; }
	FX_FORCE_INLINE int32 GetParentBody(uint32 body_index) const { return mSegments[body_index].ParentBody; }
	FX_FORCE_INLINE bool IsAnchor(uint32 body_index) const { return mSegments[body_index].bAnchor; }
	FX_FORCE_INLINE bool IsFoot(uint32 body_index) const { return mSegments[body_index].bFoot; }

	FX_FORCE_INLINE const JPH::Quat& GetRestOrientation(uint32 body_index) const
	{
		return mSegments[body_index].RestOrientation;
	}
	FX_FORCE_INLINE eRagdollBodyMode GetBodyMode(uint32 body_index) const { return mSegments[body_index].Mode; }
	FX_FORCE_INLINE float32 GetBodyStrength(uint32 body_index) const { return mSegments[body_index].Strength; }
	FX_FORCE_INLINE float32 GetRootAssist() const { return mRootAssist; }
	FX_FORCE_INLINE bool IsBalancing() const { return mbBalancing; }
	FX_FORCE_INLINE const BalanceStatus& GetBalanceStatus() const { return mBalance.GetStatus(); }
	FX_FORCE_INLINE const RagdollRig& GetRig() const { return mRig; }

	void AddImpulse(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point);
	void WriteToSkeleton();

	void DebugDraw() const;

	bool FindBoneForSegment(JPH::BodyID body_id, uint32& out_bone_index) const;

	void OnStep(const JPH::PhysicsStepListenerContext& inContext) override;

	FX_FORCE_INLINE bool IsCreated() const { return mpRagdoll != nullptr; }
	FX_FORCE_INLINE bool IsAwake() const { return mpRagdoll->IsActive(); }
	FX_FORCE_INLINE bool IsLimp() const { return mbLimp; }
	FX_FORCE_INLINE uint32 GetSerial() const { return mSerial; }

	void Destroy();

	~Ragdoll() { Destroy(); }


private:
	void RestoreTableMasses(JPH::RagdollSettings& settings);
	void DisableSelfCollisions(JPH::RagdollSettings& settings, const JPH::Mat44* joint_matrices, float32 margin);
	bool AddToPhysicsSystem(JPH::RagdollSettings& settings);
	void CreateRootAssist(const JPH::Vec3 root_position);
	void InitBodyArrays(uint32 joint_count);
	float32 WriteInitialPose();

	void BuildRig(const RagdollMath::GravityFrame& frame);
	void FindRigLimbs(uint32& out_arm_count, uint32& out_leg_count);
	bool InitRigFeet(const RagdollMath::GravityFrame& frame);
	void InitRigSides(const RagdollMath::GravityFrame& frame, uint32 arm_count, uint32 leg_count);
	int32 FindChildBody(uint32 parent, eRagdollBodyPart part) const;
	bool FindLimbChain(uint32 root, uint32& out_middle, uint32& out_end) const;

	void MoveKinematicBodies(JPH::BodyInterface& body_interface, float32 fraction, float32 delta_time);
	void UpdateSubtrees(JPH::BodyInterface& body_interface);
	float32 GetRawSubtreeInertia(uint32 body_index, const JPH::Vec3 pivot) const;
	float32 GetSubtreeInertia(uint32 body_index, const JPH::Vec3 pivot) const;
	float32 GetSupportedInertia(uint32 body_index, const JPH::Vec3 pivot) const;
	JPH::SwingTwistConstraint* GetJoint(uint32 body_index) const;
	MotorOverride& SetMotorTarget(uint32 body_index, const JPH::Quat& parent_world, const JPH::Quat& child_world,
								  float32 frequency_scale = 1.0f, bool supported = false);
	void ApplyMotor(uint32 body_index);
	JPH::Quat LevelFoot(uint32 body_index, const JPH::Quat& relative) const;
	void ApplyGravityCompensation(JPH::BodyInterface& body_interface, const JPH::Vec3 gravity);
	void ApplyRootAssist(JPH::BodyInterface& body_interface, const JPH::Vec3 gravity);
	void ClearMotorOverrides();
	float32 GetMotorTorqueFraction(uint32 body_index) const;

	void SnapSegmentToTarget(JPH::BodyInterface& body_interface, uint32 body_index, JPH::EActivation activation);
	void StorePreviousPoses(JPH::BodyInterface& body_interface);

	/**
	 * @brief Returns the world matrix for a given body segment.
	 */
	Mat4f GetSegmentWorldMatrix(uint32 body_index, float32 step_alpha) const;

	void DrawDebugSegments(const SizedArray<RDSegmentState>& samples, SizedArray<Vec3f>& out_centers) const;
	void DrawDebugLinks(const SizedArray<Vec3f>& centers) const;
	void DrawDebugSupport(const SizedArray<RDSegmentState>& samples) const;

private:
	Ref<Skeleton> mpSkeleton { nullptr };
	JPH::Ref<JPH::Ragdoll> mpRagdoll;
	JPH::Ref<JPH::GroupFilterTable> mpSelfCollisionFilter;
	JPH::PhysicsSystem* mpPhysicsSystem = nullptr;
	uint32 mSerial = 0;

	SizedArray<RDSegmentInfo> mSegments;
	SizedArray<RDSegmentTarget> mSegmentTargets;
	SizedArray<RDSegmentState> mSegmentStates;


	SizedArray<MotorOverride> mMotorOverrides;


	/// Joint targets the balance controller last drove, faded out after it lets go so the legs do not snap
	SizedArray<JPH::Quat> mOverrideTargets;
	SizedArray<uint8> mHadOverride;
	float32 mOverrideFade = 0.0f;
	SizedArray<BodyLayer> mLayers;
	SizedArray<BodyPose> mPreviousPoses;
	float32 mTotalMass = 0.0f;
	bool mbHasTargets = false;
	bool mbHasMotors = false;
	bool mbLimp = false;

	JPH::Ref<JPH::SixDOFConstraint> mpRootAssist;
	JPH::Vec3 mRootAnchor = JPH::Vec3::sZero();
	float32 mRootAssist = 0.0f;

	RagdollRig mRig;
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
