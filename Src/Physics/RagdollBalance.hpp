#pragma once

#include "RagdollMath.hpp"

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

struct BalanceStatus
{
	eBalanceState State = eBalanceState::Off;
	eFallReason FallReason = eFallReason::None;
	uint32 Steps = 0;
	uint32 RecoverySteps = 0;
	bool bRecovering = false;
	bool bCalm = false;
};

struct RagdollLeg
{
	static constexpr uint32 scSoleCorners = RagdollMath::scSoleCorners;

	uint32 Thigh = 0;
	uint32 Shin = 0;
	uint32 Foot = 0;
	float32 Upper = 0.0f;
	float32 Lower = 0.0f;
	float32 HingeSign = 1.0f;
	float32 SideSign = 1.0f;
	JPH::Vec3 HipOffset = JPH::Vec3::sZero();
	JPH::Vec3 SoleCenter = JPH::Vec3::sZero();
	JPH::Vec3 SoleCorners[scSoleCorners];
	JPH::Quat RestFoot = JPH::Quat::sIdentity();

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
	};

public:
	bool Init(Ragdoll& ragdoll);
	void Reset();
	void Clear(Ragdoll& ragdoll);

	void Step(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time, const JPH::Vec3 gravity,
			  bool contacts_fresh);

	void DebugDraw() const;

	FX_FORCE_INLINE bool IsValid() const { return mbValid; }
	FX_FORCE_INLINE const BalanceStatus& GetStatus() const { return mStatus; }
	FX_FORCE_INLINE JPH::Vec3 GetRestForward() const { return mRestForward; }

private:
	bool InitLegs(const Ragdoll& ragdoll);
	bool InitRestPose(const Ragdoll& ragdoll);

	void GatherState(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time, const JPH::Vec3 gravity,
					 bool contacts_fresh);
	void GatherFeet(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time, bool contacts_fresh);
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

	float32 GetCaptureDistance(const RagdollLeg& leg) const;
	float32 GetLandingCaptureError(uint32 stance_leg, const JPH::Vec3 landing) const;
	JPH::Vec3 PlanLanding(uint32 swing_leg, uint32 stance_leg, float32 time_left) const;

	void DriveStanceLegs(Ragdoll& ragdoll);
	void DriveSwingLeg(Ragdoll& ragdoll);
	void DriveFreeLeg(Ragdoll& ragdoll, const RagdollLeg& leg, const JPH::Vec3 ankle, const JPH::Quat& foot);
	void SolveLeg(Ragdoll& ragdoll, const RagdollLeg& leg, const JPH::Vec3 hip, const JPH::Vec3 ankle,
				  JPH::Quat& out_thigh, JPH::Quat& out_shin) const;
	void ApplyTorsoControl(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, const JPH::Vec3 gravity);
	bool IsLoaded(const RagdollLeg& leg) const;

	JPH::Vec3 GetHeadingForward(const JPH::Quat& orientation, const JPH::Quat& rest) const;
	JPH::Quat GetHeadingRotation(const JPH::Vec3 forward) const;
	JPH::Vec3 ToGround(const JPH::Vec3 point) const;

	FX_FORCE_INLINE JPH::Vec3 Horizontal(const JPH::Vec3 v) const { return v - mUp * v.Dot(mUp); }
	FX_FORCE_INLINE Vec2f ToPlane(const JPH::Vec3 point) const
	{
		return Vec2f { point.Dot(mAxisU), point.Dot(mAxisV) };
	}
	FX_FORCE_INLINE JPH::Vec3 FromPlane(const Vec2f point) const
	{
		return mAxisU * point.X + mAxisV * point.Y + mUp * mGroundHeight;
	}

private:
	bool mbValid = false;
	RagdollLeg mLegs[2];

	uint32 mNeckBody = 0;
	uint32 mChestBody = 0;

	JPH::Vec3 mRestForward = JPH::Vec3::sAxisZ();
	JPH::Quat mRestPelvis = JPH::Quat::sIdentity();

	float32 mRestComHeight = 1.0f;
	float32 mRestWidth = 0.3f;
	float32 mFootHalfWidth = 0.045f;
	float32 mFootHalfLength = 0.1f;

	JPH::Vec3 mUp = JPH::Vec3::sAxisY();
	JPH::Vec3 mAxisU = JPH::Vec3::sAxisX();
	JPH::Vec3 mAxisV = JPH::Vec3::sAxisZ();
	JPH::Vec3 mForward = JPH::Vec3::sAxisZ();
	float32 mGravity = 9.81f;
	float32 mOmega = 3.0f;
	float32 mGroundHeight = 0.0f;
	bool mbHasGround = false;
	bool mbBodyOnGround = false;

	JPH::Vec3 mCenterOfMass = JPH::Vec3::sZero();
	JPH::Vec3 mVelocity = JPH::Vec3::sZero();
	JPH::Quat mPelvisOrientation = JPH::Quat::sIdentity();
	float32 mTrunkTilt = 0.0f;
	JPH::Vec3 mCapturePoint = JPH::Vec3::sZero();

	uint32 mSupportCount = 0;
	JPH::Vec3 mSupportCenter = JPH::Vec3::sZero();
	JPH::Vec3 mSupportTarget = JPH::Vec3::sZero();
	float32 mCaptureOutside = 0.0f;

	Swing mSwing;
	float32 mDoubleSupportTime = 0.0f;
	float32 mCalmTime = 0.0f;
	float32 mSettleTime = 0.0f;
	float32 mShiftTime = 0.0f;
	int32 mShiftLeg = -1;
	BalanceStatus mStatus;
};

} // namespace fx::physics
