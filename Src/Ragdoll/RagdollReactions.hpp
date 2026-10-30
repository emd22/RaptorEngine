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
	float32 ArmSpread = 65.0f;
	float32 SpreadSpeed = 0.4f;
	/// How far the spine leans along the shove over the stumble (degrees), easing out as balance takes over
	float32 Lean = 18.0f;
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
	float32 Loosen = 0.25f;
	float32 RecoverTime = 0.6f;
	float32 Flinch = 15.0f;
	float32 FlinchTime = 0.35f;
	float32 GrabDelay = 0.3f;
	float32 GrabTime = 2.5f;
	float32 GrabReach = 1.15f;
	/// Strength the grabbing arm eases down to as the hand takes hold, so the grab steadies the body
	float32 GrabStrength = 0.5f;
	float32 HandOffset = 0.07f;
	float32 LookTime = 1.5f;
	float32 LookYaw = 80.0f;
	float32 LookPitch = 35.0f;

private:
	void LoosenAround(const BehaviourContext& ctx, uint32 body);
	void StartGrab(const BehaviourContext& ctx, const RagdollEvent& event);
	void LookAtWound(const BehaviourContext& ctx, BehaviourOutput& out) const;

	struct Wound
	{
		uint32 Body = 0;
		Vec3f LocalPoint = Vec3f::sZero;
		Vec3f LocalNormal = Vec3f::sUp;
		int32 Arm = -1;
		float32 Time = 0.0f;
	};

	SizedArray<float32> mLoosen;
	Wound mWound;
	float32 mFlinchTime = -1.0f;
	float32 mFlinchSide = 0.0f;
	float32 mFlinchPitch = 0.0f;
	int32 mLookBody = -1;
	Vec3f mLookLocal = Vec3f::sZero;
	float32 mLookTime = 0.0f;
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
	float32 MinTime = 1.0f;
	float32 MaxTime = 3.0f;
	float32 HeadTime = 0.25f;
	float32 BalanceStrength = 0.5f;

private:
	float32 mDuration = 1.0f;
	bool mbHeadShot = false;
	bool mbFinished = false;
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
