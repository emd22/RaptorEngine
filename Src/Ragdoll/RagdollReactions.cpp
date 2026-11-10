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

constexpr float32 scFlinchAttack = 0.05f;
constexpr float32 scLookAttack = 0.1f;
constexpr float32 scGrabRamp = 0.3f;
constexpr float32 scFallTimeout = 4.0f;

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

float32 AdvanceTimer(float32 time, float32 delta_time, float32 total)
{
	constexpr float32 scTimerFinished = -1.0f;

	if (time < 0.0f) {
		return scTimerFinished;
	}

	time += delta_time;

	if (time >= total) {
		return scTimerFinished;
	}

	return time;
}

int32 FindLeg(const BehaviourContext& ctx, uint32 body)
{
	for (uint32 leg = 0; leg < RagdollRig::scLimbCount; leg++) {
		const RagdollRigLeg& chain = ctx.pRig->Legs[leg];

		if (body == chain.Thigh || body == chain.Shin || body == chain.Foot) {
			return static_cast<int32>(leg);
		}
	}

	return -1;
}

float32 OutsideDeadZone(float32 value, float32 dead_zone)
{
	if (value > dead_zone) {
		return value - dead_zone;
	}

	if (value < -dead_zone) {
		return value + dead_zone;
	}

	return 0.0f;
}

bool IsInFront(const BehaviourContext& ctx, const FVec3 point)
{
	constexpr float32 scBehindMargin = -0.1f;

	const FVec3 chest = ctx.Bodies[ctx.pRig->Chest].Center;
	return ctx.Horizontal(point - chest).Dot(ctx.Horizontal(ctx.Forward)) > scBehindMargin;
}

bool IsLandingEvent(const RagdollEvent& event)
{
	return event.Type == eRagdollEvent::Impact || event.Type == eRagdollEvent::OnGround;
}

void ScaleArmLimbs(BehaviourOutput& out, const RagdollRigArm& arm, float32 scale)
{
	out.ScaleStrength(arm.Upper, scale);
	out.ScaleStrength(arm.Lower, scale);
	out.ScaleStrength(arm.Hand, scale);
}

} // namespace

/////////////////////////////////////
// Body has fallen
/////////////////////////////////////

FallenBehaviour::FallenBehaviour() : RagdollBehaviour("fallen", 0.0f)
{
	// Starts at full effect, so the fall picks up where the graded fade left the muscles
	FadeIn = 0.0f;
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
	AddSetting("lean", &Lean);
	AddSetting("lean_speed", &LeanSpeed);
	AddSetting("head_stabilise", &HeadStabilise);
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

	// The spine leans along the shove, so a shot from behind tips the body forward rather than back at the shooter
	const FVec3 push = ctx.Horizontal(ctx.pRagdoll->GetStaggerVelocity());
	const float32 push_speed = push.Length();
	float32 lean = MathUtil::DegToRad(Lean) * ctx.pBalance->Reaction;

	// Fades with the stagger drift, so the body stays tipped for as long as the shove is carrying it
	if (LeanSpeed > 0.0f) {
		lean *= MathUtil::Clamp(push_speed / LeanSpeed, 0.0f, 1.0f);
	}

	if (push_speed > 0.0f && lean > 0.0f) {
		const FVec3 axis = ctx.Up.Cross(push);

		for (uint32 i = 0; i < ctx.pRig->SpineCount; i++) {
			out.AddWorldRotation(ctx, ctx.pRig->Spine[i], axis, lean / static_cast<float32>(ctx.pRig->SpineCount));
		}

		// The head turns back against the lean in part, so it stays closer to level while the body tips
		out.AddWorldRotation(ctx, ctx.pRig->Neck, axis, -lean * HeadStabilise);
	}

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
	AddSetting("yaw_reach", &YawReach);
	AddSetting("yaw_scale", &YawScale);
	AddSetting("yaw_dead_zone", &YawDeadZone);
	AddSetting("tip_dead_zone", &TipDeadZone);
	AddSetting("tip_scale", &TipScale);
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
	float32 target_yaw = 0.0f;

	if (is_recovering) {
		const FVec3 error = ctx.Horizontal(FVec3::FromJolt(ctx.pBalance->CaptureError));
		const float32 length = error.Length();
		FVec3 outside = FVec3::sZero;

		if (length > DeadZone) {
			outside = error * ((length - DeadZone) / length);
		}

		// Tipping forward (positive about the chest's right axis) reaches the arms forward, so they catch the fall
		const FVec3 chest_spin = ctx.Bodies[ctx.pRig->Chest].AngularVelocity;
		const float32 tip_outside = OutsideDeadZone(chest_spin.Dot(ctx.Right), TipDeadZone);

		target_fore = MathUtil::Clamp(outside.Dot(ctx.Forward) / ErrorScale + tip_outside / TipScale, -1.0f, 1.0f);
		target_side = MathUtil::Clamp(outside.Dot(ctx.Right) / ErrorScale, -1.0f, 1.0f);
		target_yaw = MathUtil::Clamp(OutsideDeadZone(chest_spin.Dot(ctx.Up), YawDeadZone) / YawScale, -1.0f, 1.0f);
	}

	float32 blend = 1.0f;

	if (SmoothTime > 0.0f) {
		blend = 1.0f - std::exp(-ctx.DeltaTime / SmoothTime);
	}

	mFore += (target_fore - mFore) * blend;
	mSide += (target_side - mSide) * blend;
	mYaw += (target_yaw - mYaw) * blend;
}

void ArmBalanceBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	const RagdollRig& rig = *ctx.pRig;
	const float32 weight = MathUtil::Clamp(std::max({ std::abs(mFore), std::abs(mSide), std::abs(mYaw) }), 0.0f, 1.0f) *
						   Strength;

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		const RagdollRigArm& arm = rig.Arms[i];
		const float32 side = arm.SideSign;

		// A lean reaches out on the same side, the far arm lifts, and the arms swing against the chest's spin
		const float32 outward = SideReach * side * std::max(side * mSide, 0.0f);
		const float32 raised = RaiseReach * std::max(-side * mSide, 0.0f);
		const float32 forward = ForeReach * mFore - YawReach * side * mYaw;

		const FVec3 shoulder = ctx.Bodies[arm.Upper].Origin;
		const FVec3 target = shoulder + ctx.Forward * forward + ctx.Right * outward + ctx.Up * raised;

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
	AddSetting("flinch_twist", &FlinchTwist);
	AddSetting("grab_delay", &GrabDelay);
	AddSetting("grab_time", &GrabTime);
	AddSetting("grab_reach", &GrabReach);
	AddSetting("hand_offset", &HandOffset);
	AddSetting("look_time", &LookTime);
	AddSetting("look_yaw", &LookYaw);
	AddSetting("look_pitch", &LookPitch);
	AddSetting("grab_strength", &GrabStrength);
	AddSetting("gut_hunch", &GutHunch);
	AddSetting("gut_time", &GutTime);
	AddSetting("head_snap", &HeadSnap);
	AddSetting("head_snap_time", &HeadSnapTime);
	AddSetting("limb_strength", &LimbStrength);
	AddSetting("limb_time", &LimbTime);
}

void ShotReactionBehaviour::Init(const RagdollRig& rig, uint32 body_count)
{
	mLoosen.InitSize(body_count, 1.0f);
	mWound = Wound {};
	mFlinchTime = -1.0f;
	mFlinchPitch = 0.0f;
	mLookBody = -1;
	mLookTime = 0.0f;
	mGutTime = -1.0f;
	mSnapTime = -1.0f;
	mSnapSide = 0.0f;
	mSnapPitch = 0.0f;
	mInjury = Injury {};
}

void ShotReactionBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	// How far off the spine a hit has to land to give the torso its full turn (m)
	constexpr float32 scTwistLever = 0.15f;

	if (event.Type != eRagdollEvent::Hit || event.Body < 0 || static_cast<uint32>(event.Body) >= ctx.GetBodyCount()) {
		return;
	}

	StartIfIdle(ctx);

	if (!IsActive()) {
		return;
	}

	const uint32 body = static_cast<uint32>(event.Body);
	const FVec3 chest = ctx.Bodies[ctx.pRig->Chest].Center;

	LoosenAround(ctx, body);

	// A flinch already at full reach stays there rather than starting over from nothing
	if (mFlinchTime >= 0.0f) {
		mFlinchTime = std::min(mFlinchTime, scFlinchAttack);
	}
	else {
		mFlinchTime = 0.0f;
	}

	mFlinchSide = 1.0f;

	if ((event.Point - chest).Dot(ctx.Right) < 0.0f) {
		mFlinchSide = -1.0f;
	}

	// The shot pushes the body away from the shooter: a shot from the front arches back, from behind bows forward
	const FVec3 push = ctx.Horizontal(event.Direction).SafeNormalize(FVec3::sZero);
	mFlinchPitch = push.Dot(ctx.Horizontal(ctx.Forward));

	// How far to one side of the spine it landed, in the sense it turns the torso
	const FVec3 lever = ctx.Horizontal(event.Point - chest);
	mFlinchYaw = 0.0f;

	if (ctx.pRagdoll->GetBodyPart(body) != eRDBodyPart::Head && FindLeg(ctx, body) < 0) {
		mFlinchYaw = MathUtil::Clamp(lever.Cross(push).Dot(ctx.Up) / scTwistLever, -1.0f, 1.0f);
	}

	if (ctx.pRagdoll->GetBodyPart(body) != eRDBodyPart::Head) {
		// Already looking, the head turns to the new wound from where it is
		if (mLookBody >= 0 && mLookTime < LookTime) {
			mLookTime = std::min(mLookTime, scLookAttack);
		}
		else {
			mLookTime = 0.0f;
		}

		mLookBody = static_cast<int32>(body);
		mLookLocal = ctx.ToBodyLocal(body, event.Point);
	}

	StartGrab(ctx, event);

	if (event.Damage > 0.0f) {
		StartVariant(ctx, event);
	}
}

// Collisions and other non-damaging hits only get the flinch and look above, so a bump into a wall does not hurt
void ShotReactionBehaviour::StartVariant(const BehaviourContext& ctx, const RagdollEvent& event)
{
	const uint32 body = static_cast<uint32>(event.Body);

	switch (ctx.pRagdoll->GetBodyPart(body)) {
	case eRDBodyPart::Torso:
		mGutTime = 0.0f;
		break;
	case eRDBodyPart::Head:
		mSnapTime = 0.0f;
		mSnapPitch = mFlinchPitch;
		mSnapSide = mFlinchSide;
		break;
	default:
		break;
	}

	const int32 arm = ctx.FindArm(body);
	const int32 leg = FindLeg(ctx, body);

	if (arm >= 0 || leg >= 0) {
		mInjury = Injury { .Arm = arm, .Leg = leg, .Time = 0.0f };
	}
}

void ShotReactionBehaviour::LoosenAround(const BehaviourContext& ctx, uint32 body)
{
	// Strength a hit body in the legs or pelvis drops to at the least, since the whole figure is standing on them
	constexpr float32 scAnchorLoosen = 0.6f;

	const Ragdoll& ragdoll = *ctx.pRagdoll;
	const uint32 body_count = ctx.GetBodyCount();
	const float32 loosen = MathUtil::Clamp(Loosen, 0.0f, 1.0f);

	if (ragdoll.IsAnchor(body)) {
		mLoosen[body] = std::min(mLoosen[body], std::max(loosen, scAnchorLoosen));
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

	if (event.Damage <= 0.0f || ctx.pRagdoll->GetBodyPart(body) == eRDBodyPart::Head || GrabTime <= 0.0f) {
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

	const FVec3 outward = (event.Point - ctx.Bodies[body].Center).SafeNormalize(-event.Direction);

	mWound.Body = body;
	mWound.LocalPoint = ctx.ToBodyLocal(body, event.Point);
	mWound.LocalNormal = ctx.ToLocalDirection(body, outward);
	// The same hand already holding on moves to the new wound without letting go
	if (mWound.Arm == best_arm && mWound.Time >= GrabDelay) {
		mWound.Time = std::min(mWound.Time, GrabDelay + scGrabRamp);
	}
	else {
		mWound.Time = 0.0f;
	}

	mWound.Arm = best_arm;
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

	mGutTime = AdvanceTimer(mGutTime, dt, GutTime);
	mSnapTime = AdvanceTimer(mSnapTime, dt, HeadSnapTime);
	mInjury.Time = AdvanceTimer(mInjury.Time, dt, LimbTime);

	const bool is_looking = mLookBody >= 0 && mLookTime < LookTime;
	const bool is_varying = mGutTime >= 0.0f || mSnapTime >= 0.0f || mInjury.Time >= 0.0f;

	if (!is_loose && mFlinchTime < 0.0f && mWound.Arm < 0 && !is_looking && !is_varying) {
		Stop();
	}
}

void ShotReactionBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	constexpr float32 scGutAttack = 0.08f;
	constexpr float32 scSnapAttack = 0.03f;
	constexpr float32 scLimbAttack = 0.05f;

	const RagdollRig& rig = *ctx.pRig;

	// Stands on its own feet until it's finished with the wound, rather than walking off holding it
	if (!ctx.bFreePose && ctx.bAlive && IsActive()) {
		out.Balance = eBehaviourBalance::On;
	}

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

	// Sits apart from the flinch above, which fades out too fast for a turn this size to read
	const float32 twist = MathUtil::DegToRad(FlinchTwist) * mFlinchYaw *
						  Envelope(mFlinchTime, scFlinchAttack * 2.0f, FlinchTime);

	if (twist != 0.0f && rig.SpineCount > 0) {
		for (uint32 i = 0; i < rig.SpineCount; i++) {
			out.AddWorldRotation(ctx, rig.Spine[i], ctx.Up, twist / static_cast<float32>(rig.SpineCount));
		}
	}

	const float32 hunch = MathUtil::DegToRad(GutHunch) * Envelope(mGutTime, scGutAttack, GutTime);

	if (hunch > 0.0f && rig.SpineCount > 0) {
		for (uint32 i = 0; i < rig.SpineCount; i++) {
			out.AddWorldRotation(ctx, rig.Spine[i], ctx.Right, hunch / static_cast<float32>(rig.SpineCount));
		}
	}

	const float32 snap = MathUtil::DegToRad(HeadSnap) * Envelope(mSnapTime, scSnapAttack, HeadSnapTime);

	if (snap > 0.0f) {
		out.AddWorldRotation(ctx, rig.Neck, ctx.Right, mSnapPitch * snap);
		out.AddWorldRotation(ctx, rig.Neck, ctx.Forward, -mSnapSide * snap * 0.5f);
	}

	const float32 injury = Envelope(mInjury.Time, scLimbAttack, LimbTime);
	const float32 limb_scale = 1.0f - (1.0f - MathUtil::Clamp(LimbStrength, 0.0f, 1.0f)) * injury;

	if (mInjury.Arm >= 0) {
		ScaleArmLimbs(out, rig.Arms[mInjury.Arm], limb_scale);
	}

	if (mInjury.Leg >= 0) {
		const RagdollRigLeg& leg = rig.Legs[mInjury.Leg];
		out.ScaleStrength(leg.Thigh, limb_scale);
		out.ScaleStrength(leg.Shin, limb_scale);
		out.ScaleStrength(leg.Foot, limb_scale);
	}

	LookAtWound(ctx, out);

	if (mWound.Arm < 0 || mWound.Time < GrabDelay) {
		return;
	}

	const float32 grab_time = mWound.Time - GrabDelay;
	const float32 ramp_in = MathUtil::Clamp(grab_time / scGrabRamp, 0.0f, 1.0f);
	const float32 ramp_out = 1.0f - MathUtil::Clamp((grab_time - (GrabTime - scGrabRamp)) / scGrabRamp, 0.0f, 1.0f);

	const FVec3 wound = ctx.FromBodyLocal(mWound.Body, mWound.LocalPoint);
	const FVec3 normal = ctx.ToBodyDirection(mWound.Body, mWound.LocalNormal);

	// The grabbing arm eases its strength down as the hand takes hold, so the grab steadies the body rather than
	// pulling it over
	const float32 grip = MathUtil::SmoothStep(ramp_in) * ramp_out;
	const RagdollRigArm& grab_arm = rig.Arms[mWound.Arm];
	const float32 ease = 1.0f - (1.0f - GrabStrength) * grip;

	ScaleArmLimbs(out, grab_arm, ease);

	out.Hands[mWound.Arm] = BehaviourGoal { .Target = wound + normal * HandOffset,
											.Weight = MathUtil::SmoothStep(ramp_in) * ramp_out };
}

void ShotReactionBehaviour::LookAtWound(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	constexpr float32 scMinLookDistance = 0.2f;
	constexpr float32 scLookYawLever = 0.15f;

	if (mLookBody < 0) {
		return;
	}

	const RagdollRig& rig = *ctx.pRig;

	// From the neck joint and in the chest's frame, so neither the head's own turn nor a fall moves where the wound is
	const FVec3 to_wound = ctx.FromBodyLocal(static_cast<uint32>(mLookBody), mLookLocal) - ctx.Bodies[rig.Neck].Origin;
	const float32 closeness = MathUtil::Clamp(to_wound.Length() / scMinLookDistance, 0.0f, 1.0f);
	const float32 weight = Envelope(mLookTime, scLookAttack, LookTime) * MathUtil::SmoothStep(closeness);

	if (weight <= 0.0f) {
		return;
	}

	const FVec3 chest_up = ctx.GetChestUp();
	const float32 side = to_wound.Dot(ctx.Right);
	const float32 ahead = to_wound.Dot(ctx.Forward);

	// A wound under the chin has no direction of its own to turn towards and one behind can't be seen
	const float32 yaw = std::atan2(side, std::max(ahead, 0.0f) + scLookYawLever);
	const float32 pitch = std::atan2(to_wound.Dot(chest_up), std::sqrt(side * side + ahead * ahead));

	const float32 max_yaw = MathUtil::DegToRad(LookYaw);
	const float32 max_pitch = MathUtil::DegToRad(LookPitch);

	// Positive yaw about the chest's up turns the forward vector towards Right, and positive about Right pitches it
	// down
	out.AddWorldRotation(ctx, rig.Neck, chest_up, MathUtil::Clamp(yaw, -max_yaw, max_yaw) * weight);
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

	if (IsLandingEvent(event) && IsActive()) {
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
		if (ctx.bAlive && ctx.IsFalling()) {
			Start(ctx);
		}

		return;
	}

	if (!ctx.bAlive || mTime > scFallTimeout) {
		Stop();
		return;
	}

	// The balance got the body back up before it committed to a fall, so the hands let go
	if (!ctx.IsFalling() && !mLanding.bLanded) {
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
	constexpr float32 scBraceHandLift = 0.06f;
	constexpr float32 scBraceElbowBend = 0.1f;

	if (!mbHasTarget) {
		return;
	}

	const FVec3 side = (ctx.Right - mNormal * ctx.Right.Dot(mNormal)).SafeNormalize(ctx.Right);

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		const RagdollRigArm& arm = ctx.pRig->Arms[i];
		const FVec3 shoulder = ctx.Bodies[arm.Upper].Origin;

		FVec3 target = mPoint + side * (arm.SideSign * Spread) + mNormal * scBraceHandLift;
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
}

void ProtectHeadBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (IsLandingEvent(event)) {
		if (IsActive()) {
			mLanding.bLanded = true;
		}

		return;
	}

	if (!ctx.bAlive) {
		return;
	}

	if (event.Type != eRagdollEvent::LostSupport || !IsFallingBackward(ctx)) {
		return;
	}

	Start(ctx);
	mDuration = -1.0f;
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
	constexpr float32 scHeadHandSide = 0.11f;
	constexpr float32 scHeadHandForward = 0.05f;
	constexpr float32 scHeadHandDrop = 0.05f;

	const RagdollRig& rig = *ctx.pRig;
	const FVec3 head = ctx.Bodies[rig.Neck].Center;
	const FVec3 chest_up = ctx.GetChestUp();

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		const RagdollRigArm& arm = rig.Arms[i];
		const FVec3 target = head + ctx.Right * (arm.SideSign * scHeadHandSide) + ctx.Forward * scHeadHandForward -
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

	if (IsLandingEvent(event) && IsActive()) {
		Stop();
	}
}

void HighFallBehaviour::OnStart(const BehaviourContext& ctx) { mGroundedTime = 0.0f; }

void HighFallBehaviour::Update(const BehaviourContext& ctx)
{
	constexpr float32 scGroundedStopTime = 0.1f;

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
	AddSetting("death_time", &DeathTime);
}

void WritheBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type == eRagdollEvent::OnGround && ctx.bFreePose && !IsActive()) {
		Start(ctx);
	}
}

void WritheBehaviour::Update(const BehaviourContext& ctx)
{
	constexpr float32 scWritheResumeTime = 0.5f;

	if (IsActive() && !ctx.bFreePose) {
		Stop();
		return;
	}

	// OnGround only fires once per landing, which can be before the fall is declared and the body is in its free pose
	if (!IsActive() && ctx.bFreePose && ctx.bBodyOnGround && ctx.GroundTime >= scWritheResumeTime) {
		Start(ctx);
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

	mbHeadShot = event.Body >= 0 && ctx.pRagdoll->GetBodyPart(static_cast<uint32>(event.Body)) == eRDBodyPart::Head;

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

/////////////////////////////////////
// A body part hits the ground hard
/////////////////////////////////////

ImpactRelaxBehaviour::ImpactRelaxBehaviour() : RagdollBehaviour("impact_relax", 35.0f)
{
	FadeIn = 0.0f;
	FadeOut = 0.1f;

	AddSetting("relax", &Relax);
	AddSetting("recover_time", &RecoverTime);
}

void ImpactRelaxBehaviour::Init(const RagdollRig& rig, uint32 body_count) { mRelax.InitSize(body_count, 1.0f); }

void ImpactRelaxBehaviour::OnEvent(const BehaviourContext& ctx, const RagdollEvent& event)
{
	if (event.Type != eRagdollEvent::Impact || !ctx.bAlive || event.Body < 0 ||
		static_cast<uint32>(event.Body) >= ctx.GetBodyCount()) {
		return;
	}

	const uint32 body = static_cast<uint32>(event.Body);

	// Feet keep their grip, so balance still has something to stand on
	if (ctx.pRagdoll->IsFoot(body)) {
		return;
	}

	StartIfIdle(ctx);
	mRelax[body] = std::min(mRelax[body], MathUtil::Clamp(Relax, 0.0f, 1.0f));
}

void ImpactRelaxBehaviour::Update(const BehaviourContext& ctx)
{
	if (!IsActive()) {
		return;
	}

	float32 recover = 1.0f;

	if (RecoverTime > 0.0f) {
		recover = ctx.DeltaTime / RecoverTime;
	}

	bool all_back = true;

	for (float32& relax : mRelax) {
		relax = std::min(relax + recover, 1.0f);
		all_back = all_back && (relax >= 1.0f);
	}

	if (all_back) {
		Stop();
	}
}

void ImpactRelaxBehaviour::Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const
{
	for (uint32 i = 0; i < mRelax.Size; i++) {
		out.Strength[i] = mRelax[i];
	}
}

} // namespace fx::physics
