#pragma once

#include "RagdollMath.hpp"
#include "RagdollRig.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyInterface.h>

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>

namespace fx::physics {

class Ragdoll;

enum class eBalanceState : uint8
{
	Off,
	Standing,
	Stepping,
	Fallen,
};

enum class eFallReason : uint8
{
	None,
	Tilt,
	Height,
	Contact,
	StepBudget,
};

/**
 * @brief Limits of the centre of mass control: a horizontal force pair between the pelvis and the loaded feet that does
 * the hip and ankle strategy's job, since the leg motors alone barely move the pelvis
 */
struct BalanceAssist
{
	/// Largest force as a fraction of body weight
	float32 MaxForceFraction = 0.15f;
	/// The force fades out as the capture point moves this far outside the support (m). 0 never releases
	float32 ReleaseDistance = 0.1f;
	/// Once released, a braking-only force up to this fraction of body weight still slows the body while it steps
	float32 RecoveryForceFraction = 0.5f;
	/// How long a shove's stagger drift lasts (s): longer keeps a shoved body stumbling for more steps
	float32 StaggerDecayTime = 0.75f;
};

struct BalanceStatus
{
	eBalanceState State = eBalanceState::Off;
	eFallReason FallReason = eFallReason::None;
	uint32 Steps = 0;
	uint32 RecoverySteps = 0;
	bool bRecovering = false;
	bool bCalm = false;
};

/// A rig leg plus what the balance controller senses and plans for it each substep
struct BalanceLeg : RagdollRigLeg
{
	JPH::Vec3 AnklePosition = JPH::Vec3::sZero();
	JPH::Quat FootOrientation = JPH::Quat::sIdentity();
	JPH::Vec3 FootCenter = JPH::Vec3::sZero();
	JPH::Vec3 ContactCorners[scSoleCorners];
	uint32 ContactCornerCount = 0;
	float32 SoleHeight = 0.0f;
	float32 ContactAge = 0.0f;
	bool bTouching = false;
	bool bContact = false;

	bool bStance = true;
	JPH::Quat DesiredThigh = JPH::Quat::sIdentity();
};

class RagdollBalance
{
private:
	static constexpr float32 scContactHoldTime = 0.08f;
	static constexpr float32 scSwingPress = 0.03f;

	struct Swing
	{
		uint32 Leg = 0;
		float32 Time = 0.0f;
		JPH::Vec3 StartAnkle = JPH::Vec3::sZero();
		JPH::Vec3 Landing = JPH::Vec3::sZero();
		JPH::Vec3 AnkleTarget = JPH::Vec3::sZero();
		bool bLifted = false;
		bool bFixed = false;
		bool bSwapped = false;
	};

public:
	bool Init(Ragdoll& ragdoll);
	void Reset();
	void Clear(Ragdoll& ragdoll);

	void Step(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time, const JPH::Vec3 gravity,
			  bool contacts_fresh);

	void DebugDraw() const;

	FX_FORCE_INLINE bool IsValid() const { return mbValid; }
	FX_FORCE_INLINE void SetAssist(const BalanceAssist& assist) { mAssist = assist; }

	/// Velocity the body is let drift at after a shove instead of being held still, so it staggers a few steps. It
	/// decays on its own while balancing and is cleared when balance stops
	FX_FORCE_INLINE void SetStaggerVelocity(const JPH::Vec3 velocity) { mStaggerVelocity = velocity; }
	FX_FORCE_INLINE JPH::Vec3 GetStaggerVelocity() const { return mStaggerVelocity; }
	FX_FORCE_INLINE const BalanceStatus& GetStatus() const { return mStatus; }

private:
	bool InitRestPose(const Ragdoll& ragdoll);

	void GatherState(Ragdoll& ragdoll, float32 delta_time, const JPH::Vec3 gravity, bool contacts_fresh);
	void GatherFeet(Ragdoll& ragdoll, float32 delta_time, bool contacts_fresh);
	bool IsAnyBodyOnGround(Ragdoll& ragdoll) const;
	void BuildSupport();

	bool DetectFall();
	void Fall(eFallReason reason);
	void UpdateStanding(float32 delta_time);
	void UpdateStepping(float32 delta_time);
	void UpdateClosingStep(float32 delta_time);
	bool NeedsClosingStep() const;
	bool BeginStep();
	void StartSwing(uint32 swing_leg, const JPH::Vec3 landing, bool fixed);

	float32 GetWeightShare(uint32 leg_index) const;
	float32 GetCaptureDistance(const BalanceLeg& leg) const;
	float32 GetLandingCaptureError(uint32 stance_leg, const JPH::Vec3 landing) const;
	JPH::Vec3 PlanLanding(uint32 swing_leg, uint32 stance_leg, float32 time_left) const;

	void DriveStanceLegs(Ragdoll& ragdoll);
	void DriveSwingLeg(Ragdoll& ragdoll);
	void DriveFreeLeg(Ragdoll& ragdoll, const BalanceLeg& leg, const JPH::Vec3 ankle, const JPH::Quat& foot);
	void SolveLeg(Ragdoll& ragdoll, const BalanceLeg& leg, const JPH::Vec3 hip, const JPH::Vec3 ankle,
				  JPH::Quat& out_thigh, JPH::Quat& out_shin) const;
	void ApplyTorsoControl(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, const JPH::Vec3 gravity);
	void ApplyCenterOfMassControl(Ragdoll& ragdoll, JPH::BodyInterface& body_interface);
	bool IsLoaded(const BalanceLeg& leg) const;

	JPH::Vec3 GetHeadingForward(const JPH::Quat& orientation, const JPH::Quat& rest) const;
	JPH::Quat GetHeadingRotation(const JPH::Vec3 forward) const;
	JPH::Vec3 ToGround(const JPH::Vec3 point) const;

private:
	bool mbValid = false;
	BalanceAssist mAssist;
	BalanceLeg mLegs[RagdollRig::scLimbCount];

	const RagdollRig* mpRig = nullptr;

	JPH::Quat mRestPelvis = JPH::Quat::sIdentity();

	float32 mRestComHeight = 1.0f;
	float32 mRestWidth = 0.3f;

	RagdollMath::GravityFrame mFrame;
	JPH::Vec3 mForward = JPH::Vec3::sAxisZ();
	float32 mOmega = 3.0f;
	float32 mGroundHeight = 0.0f;
	bool mbHasGround = false;
	bool mbBodyOnGround = false;

	JPH::Vec3 mCenterOfMass = JPH::Vec3::sZero();
	JPH::Vec3 mVelocity = JPH::Vec3::sZero();
	JPH::Quat mPelvisOrientation = JPH::Quat::sIdentity();
	JPH::Quat mPelvisTarget = JPH::Quat::sIdentity();
	float32 mTrunkTilt = 0.0f;
	JPH::Vec3 mCapturePoint = JPH::Vec3::sZero();
	JPH::Vec3 mStaggerVelocity = JPH::Vec3::sZero();

	uint32 mSupportCount = 0;
	JPH::Vec3 mSupportCenter = JPH::Vec3::sZero();
	JPH::Vec3 mSupportTarget = JPH::Vec3::sZero();
	float32 mCaptureOutside = 0.0f;

	Swing mSwing;
	float32 mDoubleSupportTime = 0.0f;
	float32 mLoadWaitTime = 0.0f;
	float32 mCapturedTime = 0.0f;
	/// How far the stance legs bend below their usual height after a landing, easing back out
	float32 mLandingDip = 0.0f;
	float32 mCalmTime = 0.0f;
	float32 mSettleTime = 0.0f;
	float32 mShiftTime = 0.0f;
	int32 mShiftLeg = -1;
	BalanceStatus mStatus;
};

} // namespace fx::physics
