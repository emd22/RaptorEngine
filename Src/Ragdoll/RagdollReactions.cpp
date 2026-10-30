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
constexpr float32 scLookAttack = 0.1f;
constexpr float32 scMinLookDistance = 0.2f;
constexpr float32 scGrabRamp = 0.3f;
constexpr float32 scBehindMargin = -0.1f;
constexpr float32 scBraceHandLift = 0.06f;
constexpr float32 scBraceElbowBend = 0.1f;
constexpr float32 scHeadHandSide = 0.11f;
constexpr float32 scHeadHandForward = 0.05f;
constexpr float32 scHeadHandDrop = 0.05f;
constexpr float32 scFallTimeout = 4.0f;
constexpr float32 scGroundedStopTime = 0.1f;

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

bool IsInFront(const BehaviourContext& ctx, const Vec3f point)
{
	const Vec3f chest = ctx.Bodies[ctx.pRig->Chest].Center;
	return ctx.Horizontal(point - chest).Dot(ctx.Horizontal(ctx.Forward)) > scBehindMargin;
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

		const JPH::Quat actual = RelativeRotation(ctx.Bodies[parent].Orientation, ctx.Bodies[i].Orientation);

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
	FadeOut = 0.15f;

	AddSetting("min_impulse", &MinImpulse);
	AddSetting("min_time", &MinTime);
	AddSetting("arm_spread", &ArmSpread);
	AddSetting("spread_speed", &SpreadSpeed);
}

void StaggerBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	const bool is_shove = (event.Type == eRagdollEvent::Hit || event.Type == eRagdollEvent::Push);

	if (!is_shove || ctx.bFreePose || !ctx.bAlive || event.Magnitude < MinImpulse) {
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

	const float32 speed = ctx.Horizontal(ctx.Velocity).Length();
	const float32 spread = MathUtil::DegToRad(ArmSpread) * MathUtil::Clamp(speed / SpreadSpeed, 0.0f, 1.0f);

	for (const RagdollRigArm& arm : ctx.pRig->Arms) {
		out.AddWorldRotation(ctx, arm.Upper, ctx.Forward, arm.SideSign * spread);
	}
}

/////////////////////////////////////
// Arms reach to keep balance
/////////////////////////////////////

ArmBalanceBehaviour::ArmBalanceBehaviour() : RagdollBehaviour("arm_balance", 25.0f)
{
	FadeIn = 0.2f;
	FadeOut = 0.3f;

	AddSetting("fore_reach", &ForeReach);
	AddSetting("side_reach", &SideReach);
	AddSetting("raise_reach", &RaiseReach);
	AddSetting("error_scale", &ErrorScale);
	AddSetting("dead_zone", &DeadZone);
	AddSetting("smooth_time", &SmoothTime);
	AddSetting("strength", &Strength);
}

void ArmBalanceBehaviour::Update(const BehaviourContext& ctx)
{
	const eBalanceState state = ctx.pBalance->State;
	const bool is_recovering = ctx.bAlive && ctx.bBalancing &&
							   (state == eBalanceState::Standing || state == eBalanceState::Stepping);

	if (is_recovering && !IsActive()) {
		Start(ctx);
	}
	else if (!is_recovering && IsActive()) {
		Stop();
	}

	float32 target_fore = 0.0f;
	float32 target_side = 0.0f;

	if (is_recovering) {
		const Vec3f error = ctx.Horizontal(Vec3f::FromJolt(ctx.pBalance->CaptureError));
		const float32 length = error.Length();
		Vec3f outside = Vec3f::sZero;

		if (length > DeadZone) {
			outside = error * ((length - DeadZone) / length);
		}

		target_fore = MathUtil::Clamp(outside.Dot(ctx.Forward) / ErrorScale, -1.0f, 1.0f);
		target_side = MathUtil::Clamp(outside.Dot(ctx.Right) / ErrorScale, -1.0f, 1.0f);
	}

	float32 blend = 1.0f;

	if (SmoothTime > 0.0f) {
		blend = 1.0f - std::exp(-ctx.DeltaTime / SmoothTime);
	}

	mFore += (target_fore - mFore) * blend;
	mSide += (target_side - mSide) * blend;
}

void ArmBalanceBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	const RagdollRig& rig = *ctx.pRig;
	const float32 weight = MathUtil::Clamp(std::max(std::abs(mFore), std::abs(mSide)), 0.0f, 1.0f) * Strength;

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		const RagdollRigArm& arm = rig.Arms[i];
		const float32 side = arm.SideSign;

		// A lean reaches out on the same side, and the far arm lifts
		const float32 outward = SideReach * side * std::max(side * mSide, 0.0f);
		const float32 raised = RaiseReach * std::max(-side * mSide, 0.0f);
		const float32 forward = ForeReach * mFore;

		const Vec3f shoulder = ctx.Bodies[arm.Upper].Origin;
		const Vec3f target = shoulder + ctx.Forward * forward + ctx.Right * outward + ctx.Up * raised;

		out.Hands[i] = BehaviourGoal { .Target = target, .Weight = weight };
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
	AddSetting("look_time", &LookTime);
	AddSetting("look_yaw", &LookYaw);
	AddSetting("look_pitch", &LookPitch);
}

void ShotReactionBehaviour::Init(const RagdollRig& rig, uint32 body_count)
{
	mLoosen.InitSize(body_count, 1.0f);
	mWound = Wound {};
	mFlinchTime = -1.0f;
	mFlinchPitch = 0.0f;
	mLookBody = -1;
	mLookTime = 0.0f;
}

void ShotReactionBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type != eRagdollEvent::Hit || event.Body < 0 || static_cast<uint32>(event.Body) >= ctx.GetBodyCount()) {
		return;
	}

	StartIfIdle(ctx);

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

	// The shot pushes the body away from the shooter: a shot from the front arches back, from behind bows forward
	const Vec3f push = ctx.Horizontal(event.Direction).SafeNormalize(Vec3f::sZero);
	mFlinchPitch = push.Dot(ctx.Horizontal(ctx.Forward));

	if (ctx.pRagdoll->GetBodyPart(body) != eRagdollBodyPart::Head) {
		mLookBody = static_cast<int32>(body);
		mLookLocal = ctx.ToBodyLocal(body, event.Point);
		mLookTime = 0.0f;
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

		const RagdollRigArm& arm = ctx.pRig->Arms[i];
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

	if (mLookBody >= 0) {
		mLookTime += dt;
	}

	const bool is_looking = mLookBody >= 0 && mLookTime < LookTime;

	if (!is_loose && mFlinchTime < 0.0f && mWound.Arm < 0 && !is_looking) {
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
		// Positive about Right pitches the body forward, so this is negative for a shot from the front
		const float32 pitch = flinch * mFlinchPitch;

		for (uint32 i = 0; i < rig.SpineCount; i++) {
			out.AddWorldRotation(ctx, rig.Spine[i], ctx.Right, pitch * 0.5f);
			out.AddWorldRotation(ctx, rig.Spine[i], ctx.Forward, -mFlinchSide * flinch * 0.4f);
		}

		out.AddWorldRotation(ctx, rig.Neck, ctx.Right, pitch * 0.6f);
	}

	LookAtWound(ctx, out);

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

void ShotReactionBehaviour::LookAtWound(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	if (mLookBody < 0) {
		return;
	}

	const float32 weight = Envelope(mLookTime, scLookAttack, LookTime);
	const RagdollRig& rig = *ctx.pRig;
	const Vec3f head = ctx.Bodies[rig.Neck].Center;
	const Vec3f to_wound = ctx.FromBodyLocal(static_cast<uint32>(mLookBody), mLookLocal) - head;

	if (weight <= 0.0f || to_wound.Length() < scMinLookDistance) {
		return;
	}

	const Vec3f flat = ctx.Horizontal(to_wound);
	const float32 yaw = std::atan2(flat.Dot(ctx.Right), flat.Dot(ctx.Forward));
	const float32 pitch = std::atan2(to_wound.Dot(ctx.Up), flat.Length());

	const float32 max_yaw = MathUtil::DegToRad(LookYaw);
	const float32 max_pitch = MathUtil::DegToRad(LookPitch);

	// Positive yaw about Up turns the forward vector towards Right, and positive about Right pitches it down
	out.AddWorldRotation(ctx, rig.Neck, ctx.Up, MathUtil::Clamp(yaw, -max_yaw, max_yaw) * weight);
	out.AddWorldRotation(ctx, rig.Neck, ctx.Right, -MathUtil::Clamp(pitch, -max_pitch, max_pitch) * weight);
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
		StartIfIdle(ctx);
		return;
	}

	if ((event.Type == eRagdollEvent::Impact || event.Type == eRagdollEvent::OnGround) && IsActive()) {
		mLanding.bLanded = true;
	}
}

void BraceBehaviour::OnStart(const BehaviourContext& ctx)
{
	mLanding.Reset();
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

	for (const RagdollRigArm& arm : ctx.pRig->Arms) {
		mLanding.bLanded = mLanding.bLanded || ctx.Bodies[arm.Hand].bGround;
	}

	if (mLanding.bLanded) {
		if (mLanding.Advance(ctx.DeltaTime, Hold)) {
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
		const RagdollRigArm& arm = ctx.pRig->Arms[i];
		const Vec3f shoulder = ctx.Bodies[arm.Upper].Origin;

		Vec3f target = mPoint + side * (arm.SideSign * Spread) + mNormal * scBraceHandLift;
		target = target + (shoulder - target) * scBraceElbowBend;

		out.Hands[i] = BehaviourGoal { .Target = target, .Weight = 1.0f };
	}

	out.ScaleArmStrengths(*ctx.pRig, Strength);
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
		if (IsActive()) {
			mLanding.bLanded = true;
		}

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
}

void ProtectHeadBehaviour::OnStart(const BehaviourContext& ctx)
{
	mDuration = -1.0f;
	mLanding.Reset();
}

bool ProtectHeadBehaviour::IsFallingBackward(const BehaviourContext& ctx) const
{
	const bool is_backward = ctx.Velocity.Dot(ctx.Horizontal(ctx.Forward)) < -MinBackward;
	const bool lands_behind = ctx.bImpactPredicted && !IsInFront(ctx, ctx.ImpactPoint);

	return is_backward || lands_behind;
}

void ProtectHeadBehaviour::Update(const BehaviourContext& ctx)
{
	if (!IsActive()) {
		const bool is_falling = ctx.bAlive && ctx.bFreePose && ctx.bImpactPredicted && ctx.ImpactTime <= Horizon;

		if (is_falling && IsFallingBackward(ctx)) {
			Start(ctx);
		}

		return;
	}

	if (!ctx.bAlive || mTime > scFallTimeout || (mDuration > 0.0f && mTime >= mDuration)) {
		Stop();
		return;
	}

	if (mLanding.bLanded && mLanding.Advance(ctx.DeltaTime, Hold)) {
		Stop();
	}
}

void ProtectHeadBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	const RagdollRig& rig = *ctx.pRig;
	const Vec3f head = ctx.Bodies[rig.Neck].Center;
	const Vec3f chest_up = ctx.GetChestUp();

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		const RagdollRigArm& arm = rig.Arms[i];
		const Vec3f target = head + ctx.Right * (arm.SideSign * scHeadHandSide) + ctx.Forward * scHeadHandForward -
							 chest_up * scHeadHandDrop;

		out.Hands[i] = BehaviourGoal { .Target = target, .Weight = 1.0f };
	}

	const float32 tuck = MathUtil::DegToRad(Tuck);

	out.AddWorldRotation(ctx, rig.Neck, ctx.Right, tuck);

	for (uint32 i = 0; i < rig.SpineCount; i++) {
		out.AddWorldRotation(ctx, rig.Spine[i], ctx.Right, tuck * 0.3f);
	}

	out.ScaleArmStrengths(rig, Strength);
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
		StartIfIdle(ctx);
		return;
	}

	if ((event.Type == eRagdollEvent::Impact || event.Type == eRagdollEvent::OnGround) && IsActive()) {
		Stop();
	}
}

void HighFallBehaviour::OnStart(const BehaviourContext& ctx) { mGroundedTime = 0.0f; }

void HighFallBehaviour::Update(const BehaviourContext& ctx)
{
	if (!IsActive()) {
		if (ctx.bFreePose && ctx.AirTime >= MinAirTime && IsHighFall(ctx)) {
			Start(ctx);
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

	for (const RagdollRigArm& arm : rig.Arms) {
		float32 offset = 0.0f;

		if (arm.SideSign <= 0.0f) {
			offset = FX_PI;
		}

		out.AddWorldRotation(ctx, arm.Upper, ctx.Right, arm_swing * std::sin(phase + offset));
		out.AddWorldRotation(ctx, arm.Upper, ctx.Forward, arm.SideSign * MathUtil::DegToRad(30.0f));
		out.AddWorldRotation(ctx, arm.Lower, ctx.Right,
							 -MathUtil::DegToRad(20.0f + 15.0f * std::sin(phase * 1.3f + offset)));
	}

	for (const RagdollRigLeg& leg : rig.Legs) {
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

	for (const RagdollRigArm& arm : rig.Arms) {
		AddMovementNoise(arm.Clavicle, 0.3f);
		AddMovementNoise(arm.Upper, 1.2f);
		AddMovementNoise(arm.Lower, 0.8f);
		AddMovementNoise(arm.Hand, 0.4f);
	}

	for (const RagdollRigLeg& leg : rig.Legs) {
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
