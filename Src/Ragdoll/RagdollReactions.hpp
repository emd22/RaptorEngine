#pragma once

#include "RagdollBehaviour.hpp"

namespace fx::physics {

/// Time since a falling body touched down, for behaviours that hold briefly after landing
struct LandingHold
{
	bool bLanded = false;
	float32 Time = 0.0f;

	FX_FORCE_INLINE void Reset() { *this = LandingHold {}; }

	/// @returns true once `hold` seconds have passed since landing
	FX_FORCE_INLINE bool Advance(float32 delta_time, float32 hold)
	{
		Time += delta_time;
		return Time >= hold;
	}
};

class FallenBehaviour : public RagdollBehaviour
{
public:
	FallenBehaviour();

	void Init(const RagdollRig& rig, uint32 body_count) override;
	void OnStart(const BehaviourContext& ctx) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;
	bool NeedsFreePose() const override { return true; }

public:
	float32 Strength = 0.35f;
	float32 FollowTime = 0.3f;
	float32 GravityComp = 0.3f;

private:
	void CaptureFollowPose(const BehaviourContext& ctx, float32 blend);

	SizedArray<JPH::Quat> mFollow;
};

class StaggerBehaviour : public RagdollBehaviour
{
public:
	StaggerBehaviour();

	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;

public:
	float32 MinImpulse = 1.0f;
	float32 MinTime = 0.75f;
	float32 ArmSpread = 35.0f;
	float32 SpreadSpeed = 0.8f;
	/// How far the spine leans along the shove (degrees)
	float32 Lean = 8.0f;
	/// Stagger speed (m/s) that gives the full lean
	float32 LeanSpeed = 0.5f;
	/// Share of the spine lean the head turns back against. 0 make its tip with the body, 1 keeps it level
	float32 HeadStabilise = 0.6f;
};

/// Arms reach to catch a capture point that has left the support
class ArmBalanceBehaviour : public RagdollBehaviour
{
public:
	ArmBalanceBehaviour();

	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;

public:
	float32 ForeReach = 0.12f;
	float32 SideReach = 0.15f;
	float32 RaiseReach = 0.1f;
	float32 ErrorScale = 0.1f;
	float32 DeadZone = 0.03f;
	float32 SmoothTime = 0.2f;
	float32 Strength = 0.4f;
	/// How far the arms swing against the chest's spin (m), kept small so the arms only nudge the spin
	float32 YawReach = 0.08f;
	float32 YawScale = 3.0f;
	/// Spin (rad/s) the arms ignore, so a slow turn doesn't move them
	float32 YawDeadZone = 0.5f;
	/// Tip rate (rad/s) the arms ignore, so a slow sway doesn't move them
	float32 TipDeadZone = 0.8f;
	/// Tip rate (rad/s) past the dead zone that reaches the hands a full ForeReach
	float32 TipScale = 4.0f;

private:
	float32 mFore = 0.0f;
	float32 mSide = 0.0f;
	float32 mYaw = 0.0f;
};

class ShotReactionBehaviour : public RagdollBehaviour
{
public:
	ShotReactionBehaviour();

	void Init(const RagdollRig& rig, uint32 body_count) override;
	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;

public:
	float32 Loosen = 0.1f;
	float32 RecoverTime = 1.0f;
	float32 Flinch = 12.0f;
	float32 FlinchTime = 0.45f;
	/// Degrees the torso turns with a hit that lands off to one side of it
	float32 FlinchTwist = 18.0f;
	float32 GrabDelay = 0.3f;
	float32 GrabTime = 2.5f;
	float32 GrabReach = 1.15f;
	/// Strength the grabbing arm eases down to as the hand takes hold, so the grab steadies the body
	float32 GrabStrength = 0.5f;
	float32 HandOffset = 0.07f;
	float32 LookTime = 1.5f;
	float32 LookYaw = 80.0f;
	float32 LookPitch = 35.0f;
	/// Degrees the spine folds forward on a damaging torso hit, held for GutTime
	float32 GutHunch = 20.0f;
	float32 GutTime = 1.2f;
	/// Degrees the neck snaps on a damaging head hit, over HeadSnapTime
	float32 HeadSnap = 25.0f;
	float32 HeadSnapTime = 0.5f;
	/// Strength of a hit arm or leg at its weakest, recovering over LimbTime
	float32 LimbStrength = 0.5f;
	float32 LimbTime = 2.0f;

private:
	void LoosenAround(const BehaviourContext& ctx, uint32 body);
	void StartGrab(const BehaviourContext& ctx, const RagdollEvent& event);
	void StartVariant(const BehaviourContext& ctx, const RagdollEvent& event);
	void LookAtWound(const BehaviourContext& ctx, BehaviourOutput& out) const;

	struct Wound
	{
		uint32 Body = 0;
		Vec3f LocalPoint = Vec3f::sZero;
		Vec3f LocalNormal = Vec3f::sUp;
		int32 Arm = -1;
		float32 Time = 0.0f;
	};

	struct Injury
	{
		int32 Arm = -1;
		int32 Leg = -1;
		float32 Time = -1.0f;
	};

	SizedArray<float32> mLoosen;
	Wound mWound;
	float32 mFlinchTime = -1.0f;
	float32 mFlinchSide = 0.0f;
	float32 mFlinchPitch = 0.0f;
	float32 mFlinchYaw = 0.0f;
	int32 mLookBody = -1;
	Vec3f mLookLocal = Vec3f::sZero;
	float32 mLookTime = 0.0f;
	float32 mGutTime = -1.0f;
	float32 mSnapTime = -1.0f;
	float32 mSnapSide = 0.0f;
	float32 mSnapPitch = 0.0f;
	Injury mInjury;
};

class BraceBehaviour : public RagdollBehaviour
{
public:
	BraceBehaviour();

	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void OnStart(const BehaviourContext& ctx) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;

public:
	float32 Horizon = 0.7f;
	float32 Spread = 0.2f;
	float32 Strength = 2.5f;
	float32 Hold = 0.4f;
	float32 MinSpeed = 0.8f;

private:
	LandingHold mLanding;
	bool mbHasTarget = false;
	Vec3f mPoint = Vec3f::sZero;
	Vec3f mNormal = Vec3f::sUp;
};

class ProtectHeadBehaviour : public RagdollBehaviour
{
public:
	ProtectHeadBehaviour();

	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void OnStart(const BehaviourContext& ctx) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;

public:
	float32 Strength = 2.5f;
	float32 Tuck = 25.0f;
	float32 MinBackward = 0.3f;
	float32 Horizon = 0.5f;
	float32 Hold = 0.6f;

private:
	bool IsFallingBackward(const BehaviourContext& ctx) const;

	float32 mDuration = -1.0f;
	LandingHold mLanding;
};

class HighFallBehaviour : public RagdollBehaviour
{
public:
	HighFallBehaviour();

	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void OnStart(const BehaviourContext& ctx) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;
	bool NeedsFreePose() const override { return true; }

public:
	float32 MinHeight = 1.5f;
	float32 MinSpeed = 4.0f;
	float32 MinAirTime = 0.25f;
	float32 ArmSwing = 70.0f;
	float32 LegSwing = 40.0f;
	float32 Frequency = 1.6f;
	float32 Strength = 2.0f;

private:
	bool IsHighFall(const BehaviourContext& ctx) const;

	float32 mGroundedTime = 0.0f;
};

class WritheBehaviour : public RagdollBehaviour
{
public:
	WritheBehaviour();

	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;
	bool NeedsFreePose() const override { return true; }

public:
	float32 Strength = 0.6f;
	float32 Amplitude = 25.0f;
	float32 Frequency = 0.5f;
	float32 Curl = 15.0f;
	/// How long an NPC writhes on the ground before it dies (s)
	float32 DeathTime = 10.0f;
};

class DyingBehaviour : public RagdollBehaviour
{
public:
	DyingBehaviour();

	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void OnStart(const BehaviourContext& ctx) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;

	float32 GetStrengthScale() const;

	FX_FORCE_INLINE bool IsFinished() const { return mbFinished; }

public:
	float32 MinTime = 0.6f;
	float32 MaxTime = 1.6f;
	float32 HeadTime = 0.25f;
	float32 BalanceStrength = 0.5f;

private:
	float32 mDuration = 1.0f;
	bool mbHeadShot = false;
	bool mbFinished = false;
};

/// Gets a fallen body back on its feet with its own joints and contacts. A side-lying body first rolls its upper body
/// onto its front, then the chest lifts while the hands push on the floor and the feet are tucked under the hips, and
/// the legs straighten as the hands come off the floor. Once upright the body is handed to the balance controller.
class GetUpBehaviour : public RagdollBehaviour
{
public:
	GetUpBehaviour();

	void Init(const RagdollRig& rig, uint32 body_count) override;
	void OnStart(const BehaviourContext& ctx) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;
	eGetUpStage GetUpStage() const override { return mStage; }

public:
	/// How long the body lies still before it starts to get up (s)
	float32 RestTime = 1.0f;
	/// Turns a side-lying body onto its front (s). Not used for bodies already lying face up or down
	float32 RollTime = 0.6f;
	/// Lifts the chest while the hands and feet take the floor (s)
	float32 PushTime = 1.0f;
	/// Straightens the legs and takes the hands off the floor (s)
	float32 RiseTime = 1.0f;
	/// How far the chest lifts during the push (deg)
	float32 ChestLift = 35.0f;
	/// How far in front of the hips the feet are placed when tucked (m)
	float32 FootTuck = 0.2f;
	/// How far the floor sits below the lowest body centre (m)
	float32 FloorOffset = 0.1f;
	/// How long the upright body is held in place before the balance controller takes over (s)
	float32 SettleTime = 1.0f;
	/// Pelvis height above the floor when standing (m)
	float32 StandHeight = 0.95f;
	/// Most the chest may lean from vertical and still count as standing (deg)
	float32 StandLean = 25.0f;
	/// How long the body must stay upright before the get-up is finished (s)
	float32 StandHoldTime = 1.0f;
	/// How long standing may take before the get-up gives up and the normal fall logic takes over (s)
	float32 StandTimeout = 6.0f;
	/// How many times it tries to lift itself before staying down
	float32 MaxAttempts = 1.0f;

private:
	struct LiftPose
	{
		float32 RollProgress = 0.0f;
		float32 PushProgress = 0.0f;
		float32 RiseProgress = 0.0f;
		JPH::Quat Roll = JPH::Quat::sIdentity();
		JPH::Quat Raise = JPH::Quat::sIdentity();
		JPH::Quat IKThigh[RagdollRig::scLimbCount] = {};
		JPH::Quat IKShin[RagdollRig::scLimbCount] = {};
	};

	void BeginLift(const BehaviourContext& ctx);
	bool IsUpright(const BehaviourContext& ctx) const;
	LiftPose MakeLiftPose(const BehaviourContext& ctx) const;
	void ContributeLift(const BehaviourContext& ctx, BehaviourOutput& out) const;
	JPH::Quat RestWorld(const BehaviourContext& ctx, uint32 body) const;
	JPH::Quat LiftWorld(const BehaviourContext& ctx, const LiftPose& pose, uint32 body) const;
	JPH::Quat DesiredWorld(const BehaviourContext& ctx, const LiftPose& pose, uint32 body) const;
	JPH::Quat UprightRoot(const BehaviourContext& ctx) const;
	void SolveLegIK(const BehaviourContext& ctx, uint32 leg, float32 tuck, JPH::Quat& out_thigh,
					JPH::Quat& out_shin) const;
	Vec3f GetHandTarget(const BehaviourContext& ctx, uint32 arm) const;

	eGetUpStage mStage = eGetUpStage::Resting;
	float32 mStageTime = 0.0f;
	float32 mHoldTime = 0.0f;
	float32 mFloorHeight = 0.0f;
	uint32 mAttempts = 0;
	bool mbSideLying = false;
	bool mbFaceDown = true;
	JPH::Quat mRestRoot = JPH::Quat::sIdentity();
	Vec3f mRollAxis = Vec3f::sUp;
	float32 mRollAngle = 0.0f;
	SizedArray<JPH::Quat> mStartWorld;
	SizedArray<JPH::Quat> mRestPose;
};

/// Briefly loosens a body part that hits the ground hard, so it absorbs the landing instead of bouncing
class ImpactRelaxBehaviour : public RagdollBehaviour
{
public:
	ImpactRelaxBehaviour();

	void Init(const RagdollRig& rig, uint32 body_count) override;
	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;

public:
	/// Strength the impacted body drops to, 1 being full
	float32 Relax = 0.5f;
	/// How long the body takes to get back to full strength (s)
	float32 RecoverTime = 0.2f;

private:
	SizedArray<float32> mRelax;
};

} // namespace fx::physics
