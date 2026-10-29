#include "RagdollReactions.hpp"

#include "Ragdoll.hpp"
#include "RagdollMath.hpp"

#include <Core/Random.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <algorithm>
#include <cmath>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scFlinchAttack = 0.002f;
constexpr float32 scGrabRamp = 0.3f;
constexpr float32 scBehindMargin = -0.1f;
constexpr float32 scBraceHandLift = 0.06f;
constexpr float32 scBraceElbowBend = 0.1f;
constexpr float32 scHeadHandSide = 0.11f;
constexpr float32 scHeadHandForward = 0.05f;
constexpr float32 scHeadHandDrop = 0.05f;
constexpr float32 scFallTimeout = 4.0f;
constexpr float32 scGroundedStopTime = 0.1f;

Vec3f GetHorizontal(const BehaviourContext& ctx, const Vec3f v) { return v - ctx.Up * v.Dot(ctx.Up); }

float32 Envelope(float32 time, float32 attack, float32 total)
{
	if (time < 0.0f || time >= total) {
		return 0.0f;
	}

	if (time < attack) {
		return time / attack;
	}

	return 1.0f - MathUtil::SmoothStep((time - attack) / std::max(total - attack, 1e-4f));
}

Vec3f GetChestUp(const BehaviourContext& ctx) { return ctx.Forward.Cross(ctx.Right); }

bool IsInFront(const BehaviourContext& ctx, const Vec3f point)
{
	const Vec3f chest = ctx.Bodies[ctx.pRig->Chest].Center;
	return GetHorizontal(ctx, point - chest).Dot(GetHorizontal(ctx, ctx.Forward)) > scBehindMargin;
}

void ScaleArmStrengths(const RagdollRig& rig, BehaviourOutput& out, float32 scale)
{
	for (const BeRagdollArm& arm : rig.Arms) {
		out.ScaleStrength(arm.Clavicle, scale);
		out.ScaleStrength(arm.Upper, scale);
		out.ScaleStrength(arm.Lower, scale);
		out.ScaleStrength(arm.Hand, scale);
	}
}

} // namespace

/////////////////////////////////////
// Body has fallen
/////////////////////////////////////

FallenBehaviour::FallenBehaviour() : RagdollBehaviour("fallen", 0.0f)
{
	FadeIn = 0.4f;
	FadeOut = 0.4f;

	AddSetting("strength", &Strength);
	AddSetting("follow_time", &FollowTime);
	AddSetting("gravity_comp", &GravityComp);
}

void FallenBehaviour::Init(const RagdollRig& rig, uint32 body_count)
{
	mFollow.InitSize(body_count, JPH::Quat::sIdentity());
}

void FallenBehaviour::CaptureFollowPose(const BehaviourContext& ctx, float32 blend)
{
	for (uint32 i = 1; i < ctx.GetBodyCount(); i++) {
		const int32 parent = ctx.pRagdoll->GetParentBody(i);

		if (parent < 0) {
			continue;
		}

		const JPH::Quat actual = (ctx.Bodies[parent].Orientation.Conjugated() * ctx.Bodies[i].Orientation).Normalized();

		if (blend >= 1.0f) {
			mFollow[i] = actual;
		}
		else {
			mFollow[i] = mFollow[i].SLERP(actual, blend).Normalized();
		}
	}
}

void FallenBehaviour::OnStart(const BehaviourContext& ctx) { CaptureFollowPose(ctx, 1.0f); }

void FallenBehaviour::Update(const BehaviourContext& ctx)
{
	if (!ctx.bFreePose) {
		if (IsActive()) {
			Stop();
		}

		return;
	}

	if (!IsActive()) {
		Start(ctx);
		return;
	}

	float32 blend = 1.0f;

	if (FollowTime > 0.0f) {
		blend = 1.0f - std::exp(-ctx.DeltaTime / FollowTime);
	}

	CaptureFollowPose(ctx, blend);
}

void FallenBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	out.bAdditive = false;

	for (uint32 i = 1; i < ctx.GetBodyCount(); i++) {
		out.Pose[i] = mFollow[i];
		out.Mask[i] = 1.0f;
	}

	out.ScaleAllStrengths(Strength);

	if (ctx.bAirborne) {
		out.GravityScale = 0.0f;
	}
	else {
		out.GravityScale = GravityComp;
	}
}

/////////////////////////////////////////
// A force has been enduced on a body,
// causing a stagger
/////////////////////////////////////////

StaggerBehaviour::StaggerBehaviour() : RagdollBehaviour("stagger", 20.0f)
{
	FadeIn = 0.1f;
	FadeOut = 0.3f;

	AddSetting("min_impulse", &MinImpulse);
	AddSetting("min_time", &MinTime);
	AddSetting("arm_spread", &ArmSpread);
	AddSetting("spread_speed", &SpreadSpeed);
}

void StaggerBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type != eRagdollEvent::Hit || ctx.bFreePose || !ctx.bAlive || event.Magnitude < MinImpulse) {
		return;
	}

	Start(ctx);
}

void StaggerBehaviour::Update(const BehaviourContext& ctx)
{
	if (!IsActive()) {
		return;
	}

	if (ctx.bFreePose) {
		Stop();
		return;
	}

	if (ctx.bBalancing && ctx.pBalance->State == eBalanceState::Fallen) {
		Stop();
		return;
	}

	if (mTime < MinTime) {
		return;
	}

	if (!ctx.bBalancing || ctx.pBalance->bCalm) {
		Stop();
	}
}

void StaggerBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	out.Balance = eBehaviourBalance::On;

	if (SpreadSpeed <= 0.0f) {
		return;
	}

	const float32 speed = GetHorizontal(ctx, ctx.Velocity).Length();
	const float32 spread = MathUtil::DegToRad(ArmSpread) * MathUtil::Clamp(speed / SpreadSpeed, 0.0f, 1.0f);

	for (const BeRagdollArm& arm : ctx.pRig->Arms) {
		out.AddWorldRotation(ctx, arm.Upper, ctx.Forward, arm.SideSign * spread);
	}
}

/////////////////////////////////////
// A body has been shot
/////////////////////////////////////

ShotReactionBehaviour::ShotReactionBehaviour() : RagdollBehaviour("shot", 30.0f)
{
	FadeIn = 0.3f;
	FadeOut = 0.2f;

	AddSetting("loosen", &Loosen);
	AddSetting("recover_time", &RecoverTime);
	AddSetting("flinch", &Flinch);
	AddSetting("flinch_time", &FlinchTime);
	AddSetting("grab_delay", &GrabDelay);
	AddSetting("grab_time", &GrabTime);
	AddSetting("grab_reach", &GrabReach);
	AddSetting("hand_offset", &HandOffset);
}

void ShotReactionBehaviour::Init(const RagdollRig& rig, uint32 body_count)
{
	mLoosen.InitSize(body_count, 1.0f);
	mWound = Wound {};
	mFlinchTime = -1.0f;
}

void ShotReactionBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type != eRagdollEvent::Hit || event.Body < 0 || static_cast<uint32>(event.Body) >= ctx.GetBodyCount()) {
		return;
	}

	if (!IsActive()) {
		Start(ctx);
	}

	if (!IsActive()) {
		return;
	}

	const uint32 body = static_cast<uint32>(event.Body);
	const Vec3f chest = ctx.Bodies[ctx.pRig->Chest].Center;

	LoosenAround(ctx, body);

	mFlinchTime = 0.0f;
	mFlinchSide = 1.0f;

	if ((event.Point - chest).Dot(ctx.Right) < 0.0f) {
		mFlinchSide = -1.0f;
	}

	StartGrab(ctx, event);
}

void ShotReactionBehaviour::LoosenAround(const BehaviourContext& ctx, uint32 body)
{
	const Ragdoll& ragdoll = *ctx.pRagdoll;
	const uint32 body_count = ctx.GetBodyCount();
	const float32 loosen = MathUtil::Clamp(Loosen, 0.0f, 1.0f);

	if (ragdoll.IsAnchor(body)) {
		mLoosen[body] = std::min(mLoosen[body], loosen);
		return;
	}

	const auto child_count = [&](int32 parent)
	{
		uint32 count = 0;

		for (uint32 i = 0; i < body_count; i++) {
			if (ragdoll.GetParentBody(i) == parent) {
				count++;
			}
		}

		return count;
	};

	int32 top = static_cast<int32>(body);

	for (int32 parent = ragdoll.GetParentBody(top);
		 parent >= 0 && !ragdoll.IsAnchor(parent) && child_count(parent) == 1; parent = ragdoll.GetParentBody(top)) {
		top = parent;
	}

	for (uint32 i = 0; i < body_count; i++) {
		for (int32 walk = static_cast<int32>(i); walk >= 0; walk = ragdoll.GetParentBody(walk)) {
			if (walk == top) {
				mLoosen[i] = std::min(mLoosen[i], loosen);
				break;
			}
		}
	}
}

void ShotReactionBehaviour::StartGrab(const BehaviourContext& ctx, const RagdollEvent& event)
{
	const uint32 body = static_cast<uint32>(event.Body);

	if (event.Damage <= 0.0f || ctx.pRagdoll->GetBodyPart(body) == eRagdollBodyPart::Head || GrabTime <= 0.0f) {
		return;
	}

	const int32 hit_arm = ctx.FindArm(body);

	int32 best_arm = -1;
	float32 best_distance = 0.0f;

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		if (static_cast<int32>(i) == hit_arm) {
			continue;
		}

		const BeRagdollArm& arm = ctx.pRig->Arms[i];
		const float32 distance = (event.Point - ctx.Bodies[arm.Upper].Origin).Length();
		const float32 reach = GrabReach * (arm.UpperLength + arm.LowerLength);

		if (distance > reach) {
			continue;
		}

		if (best_arm < 0 || distance < best_distance) {
			best_arm = static_cast<int32>(i);
			best_distance = distance;
		}
	}

	if (best_arm < 0) {
		return;
	}

	const Vec3f outward = (event.Point - ctx.Bodies[body].Center).SafeNormalize(-event.Direction);

	mWound.Body = body;
	mWound.LocalPoint = ctx.ToBodyLocal(body, event.Point);
	mWound.LocalNormal = ctx.ToLocalDirection(body, outward);
	mWound.Arm = best_arm;
	mWound.Time = 0.0f;
}

void ShotReactionBehaviour::Update(const BehaviourContext& ctx)
{
	if (!IsActive()) {
		return;
	}

	const float32 dt = ctx.DeltaTime;

	float32 recover = 1.0f;

	if (RecoverTime > 0.0f) {
		recover = dt / RecoverTime;
	}

	bool is_loose = false;

	for (float32& loosen : mLoosen) {
		loosen = std::min(loosen + recover, 1.0f);
		is_loose = is_loose || (loosen < 1.0f);
	}

	if (mFlinchTime >= 0.0f) {
		mFlinchTime += dt;

		if (mFlinchTime >= FlinchTime) {
			mFlinchTime = -1.0f;
		}
	}

	if (mWound.Arm >= 0) {
		mWound.Time += dt;

		if (mWound.Time >= GrabDelay + GrabTime) {
			mWound.Arm = -1;
		}
	}

	if (!is_loose && mFlinchTime < 0.0f && mWound.Arm < 0) {
		Stop();
	}
}

void ShotReactionBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	const RagdollRig& rig = *ctx.pRig;

	for (uint32 i = 0; i < mLoosen.Size; i++) {
		out.Strength[i] = mLoosen[i];
	}

	const float32 flinch = MathUtil::DegToRad(Flinch) * Envelope(mFlinchTime, scFlinchAttack, FlinchTime);

	if (flinch > 0.0f) {
		for (uint32 i = 0; i < rig.SpineCount; i++) {
			out.AddWorldRotation(ctx, rig.Spine[i], ctx.Right, flinch * 0.5f);
			out.AddWorldRotation(ctx, rig.Spine[i], ctx.Forward, -mFlinchSide * flinch * 0.4f);
		}

		out.AddWorldRotation(ctx, rig.Neck, ctx.Right, flinch * 0.6f);
	}

	if (mWound.Arm < 0 || mWound.Time < GrabDelay) {
		return;
	}

	const float32 grab_time = mWound.Time - GrabDelay;
	const float32 ramp_in = MathUtil::Clamp(grab_time / scGrabRamp, 0.0f, 1.0f);
	const float32 ramp_out = 1.0f - MathUtil::Clamp((grab_time - (GrabTime - scGrabRamp)) / scGrabRamp, 0.0f, 1.0f);

	const Vec3f wound = ctx.FromBodyLocal(mWound.Body, mWound.LocalPoint);
	const Vec3f normal = ctx.ToBodyDirection(mWound.Body, mWound.LocalNormal);

	out.Hands[mWound.Arm] = BehaviourGoal { .Target = wound + normal * HandOffset,
											.Weight = MathUtil::SmoothStep(ramp_in) * ramp_out };
}

/////////////////////////////////////
// A body is bracing for impact
/////////////////////////////////////

BraceBehaviour::BraceBehaviour() : RagdollBehaviour("brace", 50.0f)
{
	FadeIn = 0.35f;
	FadeOut = 0.4f;

	AddSetting("horizon", &Horizon);
	AddSetting("spread", &Spread);
	AddSetting("strength", &Strength);
	AddSetting("hold", &Hold);
	AddSetting("min_speed", &MinSpeed);
}

void BraceBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type == eRagdollEvent::LostSupport && ctx.bAlive) {
		if (!IsActive()) {
			Start(ctx);
		}

		return;
	}

	if ((event.Type == eRagdollEvent::Impact || event.Type == eRagdollEvent::OnGround) && IsActive()) {
		mbLanded = true;
	}
}

void BraceBehaviour::OnStart(const BehaviourContext& ctx)
{
	mbLanded = false;
	mLandedTime = 0.0f;
	mbHasTarget = false;
}

void BraceBehaviour::Update(const BehaviourContext& ctx)
{
	if (!IsActive()) {
		return;
	}

	if (!ctx.bAlive || mTime > scFallTimeout) {
		Stop();
		return;
	}

	for (const BeRagdollArm& arm : ctx.pRig->Arms) {
		mbLanded = mbLanded || ctx.Bodies[arm.Hand].bGround;
	}

	if (mbLanded) {
		mLandedTime += ctx.DeltaTime;

		if (mLandedTime >= Hold) {
			Stop();
		}

		return;
	}

	if (!ctx.bImpactPredicted || ctx.ImpactTime > Horizon || ctx.Velocity.Length() < MinSpeed) {
		return;
	}

	if (!IsInFront(ctx, ctx.ImpactPoint)) {
		mbHasTarget = false;
		return;
	}

	mbHasTarget = true;
	mPoint = ctx.ImpactPoint;
	mNormal = ctx.ImpactNormal;
}

void BraceBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	if (!mbHasTarget) {
		return;
	}

	const Vec3f side = (ctx.Right - mNormal * ctx.Right.Dot(mNormal)).SafeNormalize(ctx.Right);

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		const BeRagdollArm& arm = ctx.pRig->Arms[i];
		const Vec3f shoulder = ctx.Bodies[arm.Upper].Origin;

		Vec3f target = mPoint + side * (arm.SideSign * Spread) + mNormal * scBraceHandLift;
		target = target + (shoulder - target) * scBraceElbowBend;

		out.Hands[i] = BehaviourGoal { .Target = target, .Weight = 1.0f };
	}

	ScaleArmStrengths(*ctx.pRig, out, Strength);
}

/////////////////////////////////////
// Falling, body protects head
/////////////////////////////////////

ProtectHeadBehaviour::ProtectHeadBehaviour() : RagdollBehaviour("protect_head", 60.0f)
{
	FadeIn = 0.7f;
	FadeOut = 0.8f;

	AddSetting("strength", &Strength);
	AddSetting("tuck", &Tuck);
	AddSetting("min_backward", &MinBackward);
	AddSetting("horizon", &Horizon);
	AddSetting("hold", &Hold);
	AddSetting("hit_time", &HitTime);
}

void ProtectHeadBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type == eRagdollEvent::Impact || event.Type == eRagdollEvent::OnGround) {
		mbLanded = IsActive();
		return;
	}

	if (!ctx.bAlive) {
		return;
	}

	float32 duration = -1.0f;

	if (event.Type == eRagdollEvent::LostSupport) {
		if (!IsFallingBackward(ctx)) {
			return;
		}
	}
	else if (event.Type == eRagdollEvent::Hit && event.Body >= 0 &&
			 ctx.pRagdoll->GetBodyPart(static_cast<uint32>(event.Body)) == eRagdollBodyPart::Head) {
		duration = HitTime;
	}
	else {
		return;
	}

	Start(ctx);

	mDuration = duration;
	mbLanded = false;
	mLandedTime = 0.0f;
}

bool ProtectHeadBehaviour::IsFallingBackward(const BehaviourContext& ctx) const
{
	const bool is_backward = ctx.Velocity.Dot(GetHorizontal(ctx, ctx.Forward)) < -MinBackward;
	const bool lands_behind = ctx.bImpactPredicted && !IsInFront(ctx, ctx.ImpactPoint);

	return is_backward || lands_behind;
}

void ProtectHeadBehaviour::Update(const BehaviourContext& ctx)
{
	if (!IsActive()) {
		const bool is_falling = ctx.bAlive && ctx.bFreePose && ctx.bImpactPredicted && ctx.ImpactTime <= Horizon;

		if (is_falling && IsFallingBackward(ctx)) {
			Start(ctx);
			mDuration = -1.0f;
			mbLanded = false;
			mLandedTime = 0.0f;
		}

		return;
	}

	if (!ctx.bAlive || mTime > scFallTimeout || (mDuration > 0.0f && mTime >= mDuration)) {
		Stop();
		return;
	}

	if (mbLanded) {
		mLandedTime += ctx.DeltaTime;

		if (mLandedTime >= Hold) {
			Stop();
		}
	}
}

void ProtectHeadBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	const RagdollRig& rig = *ctx.pRig;
	const Vec3f head = ctx.Bodies[rig.Neck].Center;
	const Vec3f chest_up = GetChestUp(ctx);

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		const BeRagdollArm& arm = rig.Arms[i];
		const Vec3f target = head + ctx.Right * (arm.SideSign * scHeadHandSide) + ctx.Forward * scHeadHandForward -
							 chest_up * scHeadHandDrop;

		out.Hands[i] = BehaviourGoal { .Target = target, .Weight = 1.0f };
	}

	const float32 tuck = MathUtil::DegToRad(Tuck);

	out.AddWorldRotation(ctx, rig.Neck, ctx.Right, tuck);

	for (uint32 i = 0; i < rig.SpineCount; i++) {
		out.AddWorldRotation(ctx, rig.Spine[i], ctx.Right, tuck * 0.3f);
	}

	ScaleArmStrengths(rig, out, Strength);
}

/////////////////////////////////////
// Falling from a large height
/////////////////////////////////////

HighFallBehaviour::HighFallBehaviour() : RagdollBehaviour("high_fall", 40.0f)
{
	FadeIn = 0.5f;
	FadeOut = 0.6f;

	AddSetting("min_height", &MinHeight);
	AddSetting("min_speed", &MinSpeed);
	AddSetting("min_air_time", &MinAirTime);
	AddSetting("arm_swing", &ArmSwing);
	AddSetting("leg_swing", &LegSwing);
	AddSetting("frequency", &Frequency);
	AddSetting("strength", &Strength);
}

bool HighFallBehaviour::IsHighFall(const BehaviourContext& ctx) const
{
	if (!ctx.bAirborne) {
		return false;
	}

	return ctx.DropHeight > MinHeight || -ctx.Velocity.Dot(ctx.Up) > MinSpeed;
}

void HighFallBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type == eRagdollEvent::LostSupport && ctx.bFreePose && IsHighFall(ctx)) {
		if (!IsActive()) {
			Start(ctx);
		}

		return;
	}

	if ((event.Type == eRagdollEvent::Impact || event.Type == eRagdollEvent::OnGround) && IsActive()) {
		Stop();
	}
}

void HighFallBehaviour::Update(const BehaviourContext& ctx)
{
	if (!IsActive()) {
		if (ctx.bFreePose && ctx.AirTime >= MinAirTime && IsHighFall(ctx)) {
			Start(ctx);
			mGroundedTime = 0.0f;
		}

		return;
	}

	if (!ctx.bFreePose) {
		Stop();
		return;
	}

	if (ctx.bAirborne) {
		mGroundedTime = 0.0f;
		return;
	}

	mGroundedTime += ctx.DeltaTime;

	if (mGroundedTime >= scGroundedStopTime) {
		Stop();
	}
}

void HighFallBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	const RagdollRig& rig = *ctx.pRig;
	const float32 phase = FX_2PI * Frequency * mTime;
	const float32 arm_swing = MathUtil::DegToRad(ArmSwing);
	const float32 leg_swing = MathUtil::DegToRad(LegSwing);

	for (const BeRagdollArm& arm : rig.Arms) {
		float32 offset = 0.0f;

		if (arm.SideSign <= 0.0f) {
			offset = FX_PI;
		}

		out.AddWorldRotation(ctx, arm.Upper, ctx.Right, arm_swing * std::sin(phase + offset));
		out.AddWorldRotation(ctx, arm.Upper, ctx.Forward, arm.SideSign * MathUtil::DegToRad(30.0f));
		out.AddWorldRotation(ctx, arm.Lower, ctx.Right,
							 -MathUtil::DegToRad(20.0f + 15.0f * std::sin(phase * 1.3f + offset)));
	}

	for (const BeRagdollLeg& leg : rig.Legs) {
		float32 offset = FX_PI_2;

		if (leg.SideSign <= 0.0f) {
			offset += FX_PI;
		}

		const float32 leg_phase = phase * 0.8f + offset;

		out.AddWorldRotation(ctx, leg.Thigh, ctx.Right, leg_swing * std::sin(leg_phase));
		out.AddWorldRotation(ctx, leg.Shin, ctx.Right, MathUtil::DegToRad(35.0f + 25.0f * std::sin(leg_phase + 0.6f)));
	}

	for (uint32 i = 0; i < rig.SpineCount; i++) {
		out.AddWorldRotation(ctx, rig.Spine[i], ctx.Right, -MathUtil::DegToRad(8.0f));
	}

	out.ScaleAllStrengths(Strength);
}

/////////////////////////////////////////
// Body writhing after being grounded
/////////////////////////////////////////

WritheBehaviour::WritheBehaviour() : RagdollBehaviour("writhe", 10.0f)
{
	FadeIn = 0.6f;
	FadeOut = 0.5f;

	AddSetting("strength", &Strength);
	AddSetting("amplitude", &Amplitude);
	AddSetting("frequency", &Frequency);
	AddSetting("curl", &Curl);
}

void WritheBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type == eRagdollEvent::OnGround && ctx.bFreePose && !IsActive()) {
		Start(ctx);
	}
}

void WritheBehaviour::Update(const BehaviourContext& ctx)
{
	if (IsActive() && !ctx.bFreePose) {
		Stop();
	}
}

void WritheBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	const RagdollRig& rig = *ctx.pRig;

	const float32 amplitude = MathUtil::DegToRad(Amplitude);
	const float32 time = mTime * Frequency;
	const float32 curl = MathUtil::DegToRad(Curl);

	const auto AddMovementNoise = [&](uint32 body, float32 scale)
	{
		const float32 seed = static_cast<float32>(body) * 2.0f;

		out.AddWorldRotation(ctx, body, ctx.Right, amplitude * scale * GetBehaviourNoise(time, seed + 0.37f));
		out.AddWorldRotation(ctx, body, ctx.Forward,
							 amplitude * scale * 0.6f * GetBehaviourNoise(time * 0.83f, seed + 1.11f));
	};

	for (uint32 i = 0; i < rig.SpineCount; i++) {
		AddMovementNoise(rig.Spine[i], 0.5f);
		out.AddWorldRotation(ctx, rig.Spine[i], ctx.Right, curl * 0.5f);
	}

	AddMovementNoise(rig.Neck, 0.8f);

	for (const BeRagdollArm& arm : rig.Arms) {
		AddMovementNoise(arm.Clavicle, 0.3f);
		AddMovementNoise(arm.Upper, 1.2f);
		AddMovementNoise(arm.Lower, 0.8f);
		AddMovementNoise(arm.Hand, 0.4f);
	}

	for (const BeRagdollLeg& leg : rig.Legs) {
		AddMovementNoise(leg.Thigh, 1.0f);
		AddMovementNoise(leg.Shin, 0.9f);
		AddMovementNoise(leg.Foot, 0.3f);

		out.AddWorldRotation(ctx, leg.Thigh, ctx.Right, -curl);
		out.AddWorldRotation(ctx, leg.Shin, ctx.Right, curl * 1.5f);
	}

	out.ScaleAllStrengths(Strength);
}

/////////////////////////////////////
// Body dying
/////////////////////////////////////

DyingBehaviour::DyingBehaviour() : RagdollBehaviour("dying", 100.0f)
{
	FadeIn = 0.0f;
	FadeOut = 0.0f;

	AddSetting("min_time", &MinTime);
	AddSetting("max_time", &MaxTime);
	AddSetting("head_time", &HeadTime);
	AddSetting("balance_strength", &BalanceStrength);
}

void DyingBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type != eRagdollEvent::Died || IsActive()) {
		return;
	}

	Start(ctx);

	if (!IsActive()) {
		return;
	}

	mbHeadShot = event.Body >= 0 &&
				 ctx.pRagdoll->GetBodyPart(static_cast<uint32>(event.Body)) == eRagdollBodyPart::Head;

	if (mbHeadShot) {
		mDuration = HeadTime;
	}
	else {
		mDuration = MinTime + (std::max(MaxTime, MinTime) - MinTime) * FastRandUnit();
	}

	mDuration = std::max(mDuration, 1e-3f);
}

void DyingBehaviour::OnStart(const BehaviourContext& ctx)
{
	mbFinished = false;
	mbHeadShot = false;

	mDuration = std::max(MinTime, 1e-3f);
}

void DyingBehaviour::Update(const BehaviourContext& ctx)
{
	if (IsActive() && mTime >= mDuration) {
		mbFinished = true;
	}
}

float32 DyingBehaviour::GetStrengthScale() const
{
	if (!IsActive()) {
		return 1.0f;
	}

	return 1.0f - MathUtil::SmoothStep(mTime / mDuration);
}

void DyingBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	const float32 scale = GetStrengthScale();

	out.ScaleAllStrengths(scale);

	if (!ctx.bFreePose && !mbHeadShot && scale > BalanceStrength) {
		out.Balance = eBehaviourBalance::On;
	}
	else {
		out.Balance = eBehaviourBalance::Off;
	}
}

} // namespace fx::physics
