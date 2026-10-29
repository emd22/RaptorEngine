#pragma once

#include "RagdollBehaviour.hpp"

namespace fx::physics {

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
	float32 SpreadSpeed = 0.6f;
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
	float32 GrabDelay = 0.15f;
	float32 GrabTime = 2.5f;
	float32 GrabReach = 1.15f;
	float32 HandOffset = 0.07f;

private:
	void LoosenAround(const BehaviourContext& ctx, uint32 body);
	void StartGrab(const BehaviourContext& ctx, const RagdollEvent& event);

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
	bool mbLanded = false;
	float32 mLandedTime = 0.0f;
	bool mbHasTarget = false;
	Vec3f mPoint = Vec3f::sZero;
	Vec3f mNormal = Vec3f::sUp;
};

class ProtectHeadBehaviour : public RagdollBehaviour
{
public:
	ProtectHeadBehaviour();

	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
	void Update(const BehaviourContext& ctx) override;
	void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const override;

public:
	float32 Strength = 2.5f;
	float32 Tuck = 25.0f;
	float32 MinBackward = 0.3f;
	float32 Horizon = 0.5f;
	float32 Hold = 0.6f;
	float32 HitTime = 0.8f;

private:
	bool IsFallingBackward(const BehaviourContext& ctx) const;

	float32 mDuration = -1.0f;
	bool mbLanded = false;
	float32 mLandedTime = 0.0f;
};

class HighFallBehaviour : public RagdollBehaviour
{
public:
	HighFallBehaviour();

	void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) override;
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

} // namespace fx::physics
