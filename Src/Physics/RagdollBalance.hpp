#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyInterface.h>

#include <Core/Types.hpp>

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
	float32 CaptureOutside = 0.0f;
	JPH::Vec3 CenterOfMass = JPH::Vec3::sZero();
	JPH::Vec3 Velocity = JPH::Vec3::sZero();
	JPH::Vec3 CapturePoint = JPH::Vec3::sZero();
	float32 TrunkTilt = 0.0f;
	JPH::Vec3 Forward = JPH::Vec3::sAxisZ();
	float32 ComHeight = 0.0f;
	JPH::Vec3 SupportCenter = JPH::Vec3::sZero();
	JPH::Vec3 FootCenters[2] = { JPH::Vec3::sZero(), JPH::Vec3::sZero() };
	uint8 FootContacts = 0;
	bool bShifting = false;
	float32 SoleHeights[2] = { 0.0f, 0.0f };
	float32 ContactAges[2] = { 0.0f, 0.0f };
	bool bClosingStep = false;
};

class RagdollBalance
{
public:
	bool Init(Ragdoll& ragdoll);
	void Reset();
	void Clear(Ragdoll& ragdoll);

	void Step(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time, const JPH::Vec3 gravity,
			  bool contacts_fresh);

	void DebugDraw() const;

	FX_FORCE_INLINE bool IsValid() const { return mbValid; }
	FX_FORCE_INLINE const BalanceStatus& GetStatus() const { return mStatus; }

private:
	static constexpr uint32 scLegCount = 2;
	static constexpr uint32 scSoleCorners = 4;

	struct Leg
	{
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

		bool bContact = false;
		bool bTouching = false;
		float32 ContactAge = 0.0f;
		float32 SoleHeight = 0.0f;
		JPH::Quat DesiredThigh = JPH::Quat::sIdentity();
		bool bStance = true;
		JPH::Vec3 AnklePosition = JPH::Vec3::sZero();
		JPH::Quat FootOrientation = JPH::Quat::sIdentity();
		JPH::Vec3 FootCenter = JPH::Vec3::sZero();
		JPH::Vec3 ContactCorners[scSoleCorners];
		uint32 ContactCornerCount = 0;
	};

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

	void GatherState(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time,
					 const JPH::Vec3 gravity, bool contacts_fresh);
	void BuildSupport();
	void UpdateStatus();
	bool DetectFall();
	void Fall(eFallReason reason);
	void UpdateStanding(float32 delta_time);
	void UpdateStepping(float32 delta_time);
	bool BeginStep();
	void StartSwing(uint32 swing_leg, const JPH::Vec3 landing, bool fixed);
	bool NeedsClosingStep() const;
	void UpdateClosingStep(float32 delta_time);
	float32 GetCaptureDistance(const Leg& leg) const;
	float32 GetLandingCaptureError(uint32 stance_leg, const JPH::Vec3 landing) const;
	JPH::Vec3 PlanLanding(uint32 swing_leg, uint32 stance_leg, float32 time_left) const;
	JPH::Vec3 GetHeadingForward(const JPH::Quat& orientation, const JPH::Quat& rest) const;
	JPH::Quat GetHeadingRotation(const JPH::Vec3 forward) const;
	JPH::Vec3 ToGround(const JPH::Vec3 point) const;

	void DriveStanceLegs(Ragdoll& ragdoll);
	void DriveSwingLeg(Ragdoll& ragdoll);
	void DriveFreeLeg(Ragdoll& ragdoll, const Leg& leg, const JPH::Vec3 ankle, const JPH::Quat& foot);
	bool IsLoaded(const Leg& leg) const;
	void SolveLeg(Ragdoll& ragdoll, const Leg& leg, const JPH::Vec3 hip, const JPH::Vec3 ankle, JPH::Quat& out_thigh,
				  JPH::Quat& out_shin) const;
	void ApplyTorsoControl(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, const JPH::Vec3 gravity);

	JPH::Vec3 Horizontal(const JPH::Vec3 v) const { return v - mUp * v.Dot(mUp); }

	bool mbValid = false;
	Leg mLegs[scLegCount];
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
	JPH::Vec3 mPelvisAngularVelocity = JPH::Vec3::sZero();
	float32 mTrunkTilt = 0.0f;

	JPH::Vec3 mSupport[scLegCount * scSoleCorners + 1];
	uint32 mSupportCount = 0;
	JPH::Vec3 mSupportCenter = JPH::Vec3::sZero();
	JPH::Vec3 mSupportTarget = JPH::Vec3::sZero();
	JPH::Vec3 mCapturePoint = JPH::Vec3::sZero();
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
