#include "RagdollBalance.hpp"

#include "Ragdoll.hpp"

#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scNearGround = 0.02f;
constexpr float32 scMinPendulumHeight = 0.3f;

/// Swing times the planner picks between (s). It takes the slowest that lands where the body needs the foot
constexpr float32 scMinSwingTime = 0.2f;
constexpr float32 scMaxSwingTime = 0.44f;
constexpr float32 scMaxStepLength = 0.65f;
/// Stagger speed (m/s) a shove needs before the body takes a catch step whether it has to or not
constexpr float32 scStumbleStepSpeed = 0.3f;

/// A trunk lean past this steps even while the capture point is still over the feet, at most once per scLeanStepTime
constexpr float32 scLeanStepTilt = MathUtil::DegToRad(10.0f);

Vec2f GetStancePressure(const BalanceLeg& stance, const GravityFrame& frame, const Vec2f capture)
{
	Vec2f points[BodyGroundContact::scMaxPoints];
	Vec2f hull[BodyGroundContact::scMaxPoints + 1];
	uint32 count = 0;

	for (uint32 i = 0; i < stance.ContactCount; i++) {
		points[count++] = frame.ToPlane(stance.ContactPoints[i]);
	}

	if (count == 0) {
		points[count++] = frame.ToPlane(stance.FootCenter);
	}

	count = BuildHull(points, count, hull);

	float32 unused_distance = 0.0f;
	return ClosestOnHull(hull, count, capture, unused_distance);
}

Vec2f ExtrapolateCapture(const Vec2f pressure, const Vec2f capture, float32 growth)
{
	return pressure + (capture - pressure) * growth;
}

} // namespace

bool RagdollBalance::Init(Ragdoll& ragdoll)
{
	mbValid = false;
	mpRig = &ragdoll.GetRig();

	if (!mpRig->bCanBalance) {
		return false;
	}

	mGroundFrame = GravityFrame::FromPhysics();

	for (uint32 i = 0; i < RagdollRig::scLimbCount; i++) {
		mLegs[i] = BalanceLeg {};
		static_cast<RagdollRigLeg&>(mLegs[i]) = mpRig->Legs[i];
	}

	mForward = mpRig->RestForward;

	if (!InitRestPose(ragdoll)) {
		return false;
	}

	mbValid = true;
	Reset();

	return true;
}

bool RagdollBalance::InitRestPose(const Ragdoll& ragdoll)
{
	const SizedArray<RDSegmentInfo>& bodies = ragdoll.mSegments;
	const RDSegmentInfo& pelvis = bodies[0];

	mRestPelvis = pelvis.RestOrientation;

	const JPH::Vec3 side = mGroundFrame.Up.Cross(mpRig->RestForward);
	float32 rest_ground = std::numeric_limits<float32>::max();

	for (const BalanceLeg& leg : mLegs) {
		const RDSegmentInfo& foot = bodies[leg.Foot];

		for (const JPH::Vec3& corner : leg.SoleCorners) {
			rest_ground = std::min(rest_ground,
								   (foot.RestPosition + foot.RestOrientation * corner).Dot(mGroundFrame.Up));
		}
	}

	const JPH::Vec3 rest_foot_a = bodies[mLegs[0].Foot].RestPosition +
								  bodies[mLegs[0].Foot].RestOrientation * mLegs[0].SoleCenter;
	const JPH::Vec3 rest_foot_b = bodies[mLegs[1].Foot].RestPosition +
								  bodies[mLegs[1].Foot].RestOrientation * mLegs[1].SoleCenter;

	mRestWidth = std::abs((rest_foot_a - rest_foot_b).Dot(side));

	JPH::Vec3 rest_com = JPH::Vec3::sZero();
	const float32 total_mass = ragdoll.GetTotalMass();

	for (const RDSegmentInfo& body : bodies) {
		rest_com += (body.RestPosition + body.RestOrientation * body.LocalCenter) * body.Mass;
	}

	if (total_mass <= 0.0f) {
		return false;
	}

	mRestComHeight = std::max((rest_com / total_mass).Dot(mGroundFrame.Up) - rest_ground, scMinPendulumHeight);

	return true;
}

void RagdollBalance::Reset()
{
	// Leaves the stagger and stumble alone: the hit that wakes balance sets them before it gets here, and Clear() is
	// what drops them
	for (BalanceLeg& leg : mLegs) {
		leg.bStance = true;
		leg.bContact = false;
		leg.bTouching = false;
		leg.ContactAge = scContactHoldTime;
		leg.ContactCount = 0;
	}

	mSwing = Swing {};
	mDoubleSupportTime = 0.0f;
	mLoadWaitTime = 0.0f;
	mFailedSwings = 0;
	mLandingDip = 0.0f;
	mLift = 0.0f;
	mbHasSmoothedPelvisVelocity = false;
	mUnviableTime = 0.0f;
	mCalmTime = 0.0f;
	mSettleTime = 0.0f;
	mShiftTime = 0.0f;
	mShiftLeg = -1;
	mbBodyOnGround = false;
	mSupportCount = 0;
	mbHasGround = false;
	mbHasGroundProbe = false;
	mStatus = BalanceStatus {};

	if (mbValid) {
		mStatus.State = eBalanceState::Standing;
	}
}

void RagdollBalance::Clear(Ragdoll& ragdoll)
{
	ragdoll.ClearMotorOverrides();
	mStaggerVelocity = JPH::Vec3::sZero();
	mStumbleTime = 0.0f;
	mReactionElapsed = 0.0f;
	mbStumbleStepped = false;
	mStatus.Stumble = 0.0f;
	mStatus.FallProgress = 0.0f;

	if (mStatus.State != eBalanceState::Fallen) {
		mStatus.State = eBalanceState::Off;
	}
}

void RagdollBalance::Step(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time,
						  const JPH::Vec3 gravity, bool contacts_fresh)
{
	constexpr float32 scLandingDipTime = 0.25f;
	constexpr float32 scCalmTime = 0.25f;

	ragdoll.ClearMotorOverrides();

	if (!mbValid || mStatus.State == eBalanceState::Off || mStatus.State == eBalanceState::Fallen) {
		return;
	}

	for (const RDSegmentInfo& body : ragdoll.mSegments) {
		if (body.Mode != eRagdollBodyMode::Powered) {
			return;
		}
	}

	mDeltaTime = delta_time;

	GatherState(ragdoll, delta_time, gravity, contacts_fresh);
	BuildSupport();

	const float32 stagger_decay = std::max(mAssist.StaggerDecayTime, 1e-3f);
	mStaggerVelocity = mGroundFrame.Horizontal(mStaggerVelocity) * std::exp(-delta_time / stagger_decay);
	mLandingDip *= std::exp(-delta_time / scLandingDipTime);
	mStumbleTime = std::max(mStumbleTime - delta_time, 0.0f);

	mStatus.Stumble = 0.0f;
	mStatus.Reaction = 1.0f;

	if (mAssist.StumbleTime > 0.0f) {
		mStatus.Stumble = mStumbleTime / mAssist.StumbleTime;
	}

	if (mStumbleTime > 0.0f && mAssist.ReactionTime > 0.0f) {
		mReactionElapsed += delta_time;
		mStatus.Reaction = MathUtil::Clamp(mReactionElapsed / mAssist.ReactionTime, 0.0f, 1.0f);
	}

	if (!DetectFall()) {
		if (mStatus.State == eBalanceState::Standing) {
			UpdateStanding(delta_time);
		}
		else if (mStatus.State == eBalanceState::Stepping) {
			UpdateStepping(delta_time);
		}
	}

	mStatus.bCalm = (mStatus.State == eBalanceState::Standing) && (mCalmTime > scCalmTime) && (mShiftLeg < 0);

	for (BalanceLeg& leg : mLegs) {
		if (leg.bStance && !IsLoaded(leg)) {
			mStatus.HangingTime += delta_time;
		}

		if (leg.bStance && contacts_fresh && leg.bTouching != leg.bWasTouching) {
			mStatus.ContactFlips++;
		}

		if (contacts_fresh) {
			leg.bWasTouching = leg.bTouching;
		}
	}

	if (mStatus.State == eBalanceState::Fallen) {
		return;
	}

	DriveStanceLegs(ragdoll);

	if (mStatus.State == eBalanceState::Stepping) {
		DriveSwingLeg(ragdoll);
	}

	ApplyTorsoControl(ragdoll, body_interface, gravity);
	ApplyLegForces(ragdoll, body_interface);
	DriveHead(ragdoll);
	ApplyTrunkSpinDamping(ragdoll, body_interface);
}

void RagdollBalance::GatherState(Ragdoll& ragdoll, float32 delta_time, const JPH::Vec3 gravity, bool contacts_fresh)
{
	constexpr float32 scMaxStaggerDrift = 0.15f;

	mGroundFrame = GravityFrame::FromGravity(gravity);

	ragdoll.GetMassAverage(ragdoll.mSegmentStates, mCenterOfMass, mVelocity);

	mPelvisOrientation = ragdoll.mSegmentStates[0].Orientation;

	// Plan around the way the body actually faces and keep only the animated pelvis tilt: a stumble can turn the body
	// well away from the held animation, and holding the animated heading spends the hips' torque fighting the turn
	// while the planner's left and right no longer match the legs
	const JPH::Quat& animated = ragdoll.mSegmentTargets[0].Orientation;
	mForward = GetHeadingForward(mPelvisOrientation, mRestPelvis);
	mPelvisTarget = (GetHeadingRotation(mForward) *
					 GetHeadingRotation(GetHeadingForward(animated, mRestPelvis)).Conjugated() * animated)
						.Normalized();

	GatherFeet(ragdoll, delta_time, contacts_fresh);

	if (contacts_fresh) {
		mbBodyOnGround = IsAnyBodyOnGround(ragdoll);
	}

	const float32 height = std::max(mCenterOfMass.Dot(mGroundFrame.Up) - mFallGround, scMinPendulumHeight);
	mOmega = std::sqrt(mGroundFrame.Gravity / height);

	// A stagger adds the distance it still has to drift, so a shove the legs soaked up still gets stepped into. It is
	// capped so the steps aim at a point they can reach rather than one the body is not actually heading for
	JPH::Vec3 stagger_drift = mGroundFrame.Horizontal(mStaggerVelocity) * mAssist.StaggerDecayTime;

	// A shove too small to need a catch step doesn't get to ask for one
	if (mGroundFrame.Horizontal(mStaggerVelocity).Length() < scStumbleStepSpeed) {
		stagger_drift = JPH::Vec3::sZero();
	}
	const float32 drift_length = stagger_drift.Length();

	if (drift_length > scMaxStaggerDrift) {
		stagger_drift *= scMaxStaggerDrift / drift_length;
	}

	mCapturePoint = ToGround(mCenterOfMass + mGroundFrame.Horizontal(mVelocity) / mOmega + stagger_drift);

	const JPH::Quat chest = ragdoll.mSegmentStates[mpRig->Chest].Orientation;
	const JPH::Vec3 chest_up = (chest * ragdoll.mSegments[mpRig->Chest].RestOrientation.Conjugated()) * mGroundFrame.Up;

	mTrunkTilt = std::acos(MathUtil::Clamp(chest_up.Dot(mGroundFrame.Up), -1.0f, 1.0f));

	mLeanDirection = mForward;

	if (mTrunkTilt > scLeanStepTilt) {
		mLeanDirection = mGroundFrame.Horizontal(chest_up).NormalizedOr(mForward);
	}
}

void RagdollBalance::GatherFeet(Ragdoll& ragdoll, float32 delta_time, bool contacts_fresh)
{
	// Cosine of the tilt under which a sole counts as lying flat
	constexpr float32 scFlatSoleCos = 0.97f;
	// How long the fall height takes to follow a step up (s), and the rise it ignores as contact noise (m)
	constexpr float32 scFallGroundRiseTime = 0.4f;
	constexpr float32 scFallGroundTolerance = 0.015f;

	float32 contact_ground = std::numeric_limits<float32>::max();
	const BalanceLeg* ground_leg = nullptr;

	for (BalanceLeg& leg : mLegs) {
		const RDSegmentState& foot = ragdoll.mSegmentStates[leg.Foot];
		const JPH::Quat orientation = foot.Orientation;

		leg.AnklePosition = foot.Origin;
		leg.FootOrientation = orientation;
		leg.FootCenter = leg.AnklePosition + orientation * leg.SoleCenter;

		leg.ContactAge += delta_time;

		if (contacts_fresh) {
			BodyGroundContact contact;
			leg.bTouching = ragdoll.GetBodyGroundContact(leg.Foot, contact) &&
							Ragdoll::IsSupportNormal(contact, mGroundFrame.Up, scSupportNormalMinUp);

			if (leg.bTouching) {
				leg.ContactAge = 0.0f;
				leg.ContactCount = contact.PointCount;
				leg.ContactNormal = JPH::Vec3(FVec3::ToJolt(contact.Normal));
				leg.ContactFriction = contact.Friction;
				leg.SurfaceHeight = std::numeric_limits<float32>::max();

				for (uint32 i = 0; i < contact.PointCount; i++) {
					leg.ContactPoints[i] = contact.Points[i];
					leg.SurfaceHeight = std::min(leg.SurfaceHeight, contact.Points[i].Dot(mGroundFrame.Up));
				}
			}
		}

		leg.SoleUp = (orientation * leg.RestFoot.Conjugated()) * mGroundFrame.Up;

		leg.SoleHeight = std::numeric_limits<float32>::max();

		for (uint32 corner = 0; corner < scSoleCorners; corner++) {
			leg.SoleWorld[corner] = leg.AnklePosition + orientation * leg.SoleCorners[corner];
			leg.SoleHeight = std::min(leg.SoleHeight, leg.SoleWorld[corner].Dot(mGroundFrame.Up));
		}

		if (leg.ContactAge < scContactHoldTime && leg.SurfaceHeight < contact_ground) {
			contact_ground = leg.SurfaceHeight;
			ground_leg = &leg;
		}
	}

	if (ground_leg != nullptr) {
		const bool had_ground = mbHasGround;

		mGroundHeight = contact_ground;
		mbHasGround = true;

		float32 probe = 0.0f;

		if (ProbeSurface(ground_leg->FootCenter, probe)) {
			mGroundProbe = probe;
			mbHasGroundProbe = true;
		}

		if (!had_ground || mGroundHeight < mFallGround + scFallGroundTolerance) {
			mFallGround = mGroundHeight;
		}
		else {
			mFallGround += (mGroundHeight - mFallGround) * (1.0f - std::exp(-delta_time / scFallGroundRiseTime));
		}
	}
	else if (!mbHasGround) {
		mGroundHeight = std::min(mLegs[0].FootCenter.Dot(mGroundFrame.Up), mLegs[1].FootCenter.Dot(mGroundFrame.Up));
		mFallGround = mGroundHeight;
	}

	for (BalanceLeg& leg : mLegs) {
		const bool recently_touched = (leg.ContactAge < scContactHoldTime);

		// A foot off the ground is measured against the surface under it
		if (!recently_touched) {
			leg.SurfaceHeight = GetGroundAt(leg.FootCenter);
		}

		leg.bContact = recently_touched || (mbHasGround && leg.SoleHeight - leg.SurfaceHeight < scNearGround);

		if (recently_touched) {
			// A sole lying flat can take weight anywhere on it, whichever corners touched this step. The solver's
			// contact points jump from corner to corner as it settles, and the support shouldn't jump with them
			if (leg.SoleUp.Dot(mGroundFrame.Up) > scFlatSoleCos) {
				leg.ContactCount = 0;

				for (const JPH::Vec3& corner : leg.SoleWorld) {
					leg.ContactPoints[leg.ContactCount++] = corner;
				}
			}

			continue;
		}

		// Hovering just above the ground the foot is about to bear weight on the corners nearest it
		leg.ContactCount = 0;

		if (leg.bContact) {
			for (const JPH::Vec3& corner : leg.SoleWorld) {
				if (corner.Dot(mGroundFrame.Up) - leg.SurfaceHeight < scNearGround) {
					leg.ContactPoints[leg.ContactCount++] = corner;
				}
			}
		}
	}
}

bool RagdollBalance::IsAnyBodyOnGround(Ragdoll& ragdoll) const
{
	for (uint32 i = 0; i < ragdoll.mSegments.Size; i++) {
		const eRDBodyPart part = ragdoll.mSegments[i].Part;

		if (part != eRDBodyPart::Torso && part != eRDBodyPart::Head) {
			continue;
		}

		if (ragdoll.IsBodyOnGround(i, mGroundFrame.Up, scSupportNormalMinUp)) {
			return true;
		}
	}

	return false;
}

void RagdollBalance::BuildSupport()
{
	constexpr float32 scSupportShrink = 0.5f;
	constexpr float32 scCentering = 0.2f;

	constexpr uint32 scMaxPoints = 2 * BodyGroundContact::scMaxPoints;

	Vec2f points[scMaxPoints];
	uint32 count = 0;

	JPH::Vec3 foot_center_sum = JPH::Vec3::sZero();
	uint32 stance_count = 0;

	for (const BalanceLeg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		foot_center_sum += leg.FootCenter;
		stance_count++;

		for (uint32 i = 0; i < leg.ContactCount; i++) {
			points[count++] = mGroundFrame.ToPlane(leg.ContactPoints[i]);
		}
	}

	Vec2f hull[scMaxPoints + 1];
	mSupportCount = BuildHull(points, count, hull);

	if (mSupportCount == 0) {
		mSupportCenter = ToGround(foot_center_sum / static_cast<float32>(std::max(stance_count, 1u)));
		mSupportTarget = mSupportCenter;
		mCaptureOutside = 0.0f;
		mStatus.CaptureError = JPH::Vec3::sZero();
		return;
	}

	const Vec2f center = GetHullCentroid(hull, mSupportCount);

	mSupportCenter = mGroundFrame.FromPlane(center, mGroundHeight);

	const Vec2f closest = ClosestOnHull(hull, mSupportCount, mGroundFrame.ToPlane(mCapturePoint), mCaptureOutside);
	mStatus.CaptureError = mCapturePoint - mGroundFrame.FromPlane(closest, mGroundHeight);

	Vec2f shrunk[scMaxPoints + 1];

	for (uint32 i = 0; i < mSupportCount; i++) {
		shrunk[i] = center + (hull[i] - center) * scSupportShrink;
	}

	float32 unused_distance = 0.0f;
	const Vec2f held = ClosestOnHull(shrunk, mSupportCount, mGroundFrame.ToPlane(mCenterOfMass), unused_distance);
	const Vec2f target = held + (center - held) * scCentering;

	mSupportTarget = mGroundFrame.FromPlane(target, mGroundHeight);
}

bool RagdollBalance::DetectFall()
{
	constexpr float32 scFallTilt = MathUtil::DegToRad(45.0f);
	// Where a fall starts to build up, before it reaches the thresholds above
	constexpr float32 scFallStartTilt = MathUtil::DegToRad(22.5f);
	constexpr float32 scFallStartHeight = 0.85f;
	constexpr float32 scFallHeightFraction = 0.6f;
	constexpr float32 scFallProgressAttack = 0.06f;
	constexpr float32 scFallProgressRelease = 0.3f;
	// How long the capture point has to stay out of reach of any run of steps before it counts as a fall (s)
	constexpr float32 scUnviableTime = 0.2f;

	const float32 height = mCenterOfMass.Dot(mGroundFrame.Up) - mFallGround;
	const float32 tilt_progress = MathUtil::Clamp((mTrunkTilt - scFallStartTilt) / (scFallTilt - scFallStartTilt), 0.0f,
												  1.0f);
	// The height is only measured down to a ground that a sole is near. Airborne, mGroundHeight is the last surface
	// touched
	const bool grounded = mLegs[0].bContact || mLegs[1].bContact;
	float32 height_progress = 0.0f;

	if (grounded) {
		height_progress = MathUtil::Clamp(
			(scFallStartHeight - height / mRestComHeight) / (scFallStartHeight - scFallHeightFraction), 0.0f, 1.0f);
	}

	// Balance and the muscles fade over the approach, reaching the same point as the thresholds below. Eased so a foot
	// losing contact for a moment or a sway past the threshold doesn't reach the muscles as a jolt
	const float32 fall_progress = std::max(tilt_progress, height_progress);
	float32 fall_time = scFallProgressRelease;

	if (fall_progress > mStatus.FallProgress) {
		fall_time = scFallProgressAttack;
	}

	mStatus.FallProgress += (fall_progress - mStatus.FallProgress) * (1.0f - std::exp(-mDeltaTime / fall_time));

	eFallReason reason = eFallReason::None;

	if (mTrunkTilt > scFallTilt) {
		reason = eFallReason::Tilt;
	}
	else if (grounded && height < mRestComHeight * scFallHeightFraction) {
		reason = eFallReason::Height;
	}
	else if (mbBodyOnGround) {
		reason = eFallReason::Contact;
	}

	if (reason == eFallReason::None) {
		if (IsViable()) {
			mUnviableTime = 0.0f;
		}
		else {
			mUnviableTime += mDeltaTime;

			if (mUnviableTime > scUnviableTime) {
				reason = eFallReason::Unviable;
			}
		}
	}

	if (reason == eFallReason::None) {
		return false;
	}

	Fall(reason);
	return true;
}

void RagdollBalance::Fall(eFallReason reason)
{
	mStatus.State = eBalanceState::Fallen;
	mStatus.FallReason = reason;
}

void RagdollBalance::UpdateStanding(float32 delta_time)
{
	constexpr float32 scStepTrigger = 0.04f;
	constexpr float32 scMinDoubleSupport = 0.12f;
	// How far past the support the capture point has to be to cut a foot's time on the ground short (m)
	constexpr float32 scUrgentCapture = 0.25f;
	constexpr float32 scLeanStepTime = 0.15f;
	constexpr float32 scCalmSpeed = 0.15f;

	mDoubleSupportTime += delta_time;

	if (mCaptureOutside < 0.0f && mGroundFrame.Horizontal(mVelocity).Length() < scCalmSpeed) {
		mCalmTime += delta_time;
	}
	else {
		mCalmTime = 0.0f;
	}

	// Through a stumble the first step goes backwards with the shove, rather than waiting on the capture point
	if (mStumbleTime > 0.0f && mGroundFrame.Horizontal(mStaggerVelocity).Length() >= scStumbleStepSpeed) {
		if (!mbStumbleStepped && mStatus.Reaction >= 1.0f && mDoubleSupportTime > scMinDoubleSupport) {
			BeginStumbleStep();
		}

		return;
	}

	const bool capture_outside = mCaptureOutside > scStepTrigger &&
								 (mDoubleSupportTime > scMinDoubleSupport || mCaptureOutside > scUrgentCapture);
	const bool leaning = mTrunkTilt > scLeanStepTilt && mDoubleSupportTime > scLeanStepTime;

	if (mSupportCount > 0 && (capture_outside || leaning)) {
		mShiftLeg = -1;

		if (!BeginStep()) {
			mLoadWaitTime += delta_time;
		}

		return;
	}

	mLoadWaitTime = 0.0f;
	UpdateClosingStep(delta_time);
}

void RagdollBalance::UpdateStepping(float32 delta_time)
{
	constexpr float32 scSwingFreezeFraction = 0.75f;
	constexpr float32 scSwingTravelFraction = 0.85f;
	constexpr float32 scSwingLift = 0.12f;
	constexpr float32 scSwingOvertime = 0.15f;
	constexpr float32 scSwingPress = 0.03f;
	// Swings in a row that can miss the ground before the foot is just put down where it is
	constexpr uint32 scMaxFailedSwings = 3;
	constexpr float32 scSwingMinTouchdown = 0.6f;
	constexpr float32 scTouchdownDistance = 0.05f;
	constexpr float32 scSwapLift = 0.03f;
	// How far a landing has to be above or below where the foot left for the swing to go up first or come down last
	// (m)
	constexpr float32 scStepHeight = 0.03f;
	// Fraction of the swing by which a foot stepping up has reached its height, and from which one stepping down
	// leaves it
	constexpr float32 scStepUpEnd = 0.45f;
	constexpr float32 scStepDownStart = 0.35f;
	constexpr float32 scLandingDipPerSpeed = 0.1f;
	constexpr float32 scMaxLandingDip = 0.03f;

	BalanceLeg& leg = mLegs[mSwing.Leg];

	mSwing.Time += delta_time;

	const uint32 stance_leg = 1 - mSwing.Leg;
	const BalanceLeg& stance = mLegs[stance_leg];

	// The swing foot is still carrying the body and the stance foot is coming off the ground instead, so step with the
	// free foot rather than pivot over the planted one
	if (!mSwing.bLifted && !mSwing.bSwapped && leg.bTouching && !stance.bTouching &&
		stance.SoleHeight - stance.SurfaceHeight > scSwapLift) {
		const uint32 planted_leg = mSwing.Leg;

		leg.bStance = true;
		float32 swap_time = scMinSwingTime;
		const JPH::Vec3 swap_landing = PlanStep(stance_leg, planted_leg, swap_time);

		StartSwing(stance_leg, swap_landing, false, swap_time);
		mSwing.bSwapped = true;
		return;
	}

	const float32 phase = mSwing.Time / mSwing.Duration;

	if (!mSwing.bFixed && phase < scSwingFreezeFraction) {
		const JPH::Vec3 landing = PlanLanding(mSwing.Leg, 1 - mSwing.Leg, mSwing.Duration - mSwing.Time);

		mStatus.SwingLandingTravel += (landing - mSwing.Landing).Length();
		mSwing.Landing = landing;
	}

	const JPH::Vec3 landing_ankle = GetLandingAnkle(leg, mSwing.Landing);

	const float32 travel = MathUtil::SmoothStep(phase / scSwingTravelFraction);
	const JPH::Vec3 start = mGroundFrame.Horizontal(mSwing.StartAnkle);
	const JPH::Vec3 end = mGroundFrame.Horizontal(landing_ankle);

	// Covers its share of the distance left, so a landing that moves bends the path instead of throwing the foot along
	// it
	const float32 travel_left = 1.0f - mSwing.Travel;

	if (travel_left > 1e-4f) {
		mSwing.PathPoint += (landing_ankle - mSwing.PathPoint) * ((travel - mSwing.Travel) / travel_left);
	}
	else {
		mSwing.PathPoint = landing_ankle;
	}

	mSwing.Travel = travel;

	const float32 rise = mSwing.Landing.Dot(mGroundFrame.Up) - mSwing.StartSurface;

	if (std::abs(rise) > scStepHeight) {
		const auto get_height_travel = [rise](float32 at_phase)
		{
			if (rise > 0.0f) {
				return MathUtil::SmoothStep(at_phase / scStepUpEnd);
			}

			return MathUtil::SmoothStep((at_phase - scStepDownStart) / (scSwingTravelFraction - scStepDownStart));
		};

		const float32 height_before = get_height_travel((mSwing.Time - delta_time) / mSwing.Duration);
		const float32 height_left = 1.0f - height_before;

		if (height_left > 1e-4f) {
			mSwing.PathHeight += (landing_ankle.Dot(mGroundFrame.Up) - mSwing.PathHeight) *
								 ((get_height_travel(phase) - height_before) / height_left);
		}
		else {
			mSwing.PathHeight = landing_ankle.Dot(mGroundFrame.Up);
		}
	}
	else {
		mSwing.PathHeight = mSwing.PathPoint.Dot(mGroundFrame.Up);
	}

	float32 height = mSwing.PathHeight;

	if (phase < 1.0f) {
		height += scSwingLift * std::sin(FX_PI * phase);
	}
	else {
		height -= scSwingPress;
	}

	const JPH::Vec3 ankle_target = mGroundFrame.Horizontal(mSwing.PathPoint) + mGroundFrame.Up * height +
								   GetSwingDetour(start, end, travel);

	mStatus.PeakSwingTargetStep = std::max(mStatus.PeakSwingTargetStep, (ankle_target - mSwing.AnkleTarget).Length());
	mSwing.AnkleTarget = ankle_target;

	if (!leg.bTouching) {
		mSwing.bLifted = true;
	}

	const bool near_landing = mGroundFrame.Horizontal(leg.AnklePosition - landing_ankle).Length() < scTouchdownDistance;
	const bool touched_down = leg.bTouching &&
							  ((mSwing.bLifted && (phase > scSwingMinTouchdown || near_landing)) || phase >= 1.0f);

	if (phase >= 1.0f && !touched_down) {
		mStatus.SwingOvertime += delta_time;
	}

	if (!touched_down && mSwing.Time <= mSwing.Duration + scSwingOvertime) {
		return;
	}

	// The foot never reached its landing. Replan from where it is
	if (!touched_down) {
		mStatus.SwingRetries++;
		mFailedSwings++;

		// Short only of the floor, it carries on down as a standing leg and the hip comes down to meet it. Starting the
		// swing over would lift the foot all the way back up
		if (near_landing || mFailedSwings >= scMaxFailedSwings) {
			leg.bStance = true;
			mStatus.State = eBalanceState::Standing;
			mDoubleSupportTime = 0.0f;
			return;
		}

		float32 retry_time = scMinSwingTime;
		const JPH::Vec3 retry_landing = PlanStep(mSwing.Leg, 1 - mSwing.Leg, retry_time);

		StartSwing(mSwing.Leg, retry_landing, false, retry_time);
		return;
	}

	leg.bStance = true;
	mFailedSwings = 0;

	mStatus.State = eBalanceState::Standing;
	mStatus.Steps++;
	mDoubleSupportTime = 0.0f;

	// The knees give with the landing, more the faster the body was going, instead of catching it on straight legs
	const float32 landing_speed = mGroundFrame.Horizontal(mVelocity).Length();
	mLandingDip = std::max(mLandingDip, std::min(landing_speed * scLandingDipPerSpeed, scMaxLandingDip));
}

void RagdollBalance::UpdateClosingStep(float32 delta_time)
{
	constexpr float32 scSettleSpeed = 0.25f;
	constexpr float32 scSettleTime = 0.3f;
	constexpr float32 scSettleInside = 0.03f;
	constexpr float32 scShiftInside = 0.02f;
	constexpr float32 scShiftSpeed = 0.3f;
	constexpr float32 scShiftTimeout = 1.0f;

	if (mCaptureOutside < -scSettleInside && mGroundFrame.Horizontal(mVelocity).Length() < scSettleSpeed) {
		mSettleTime += delta_time;
	}
	else {
		mSettleTime = 0.0f;
	}

	if (mShiftLeg < 0) {
		if (mSettleTime > scSettleTime && NeedsClosingStep()) {
			const float32 distance_a = mGroundFrame.Horizontal(mCenterOfMass - mLegs[0].FootCenter).Length();
			const float32 distance_b = mGroundFrame.Horizontal(mCenterOfMass - mLegs[1].FootCenter).Length();

			mShiftLeg = 1;

			if (distance_a > distance_b) {
				mShiftLeg = 0;
			}
			mShiftTime = 0.0f;
		}

		return;
	}

	mShiftTime += delta_time;

	const uint32 swing_leg = static_cast<uint32>(mShiftLeg);
	const BalanceLeg& stance = mLegs[1 - swing_leg];

	if (GetCaptureDistance(stance) < -scShiftInside && mGroundFrame.Horizontal(mVelocity).Length() < scShiftSpeed) {
		const JPH::Vec3 side = mGroundFrame.Up.Cross(mForward) * mLegs[swing_leg].SideSign;
		StartSwing(swing_leg, ToLanding(stance.FootCenter + side * mRestWidth), true, scMaxSwingTime);
		return;
	}

	if (mShiftTime > scShiftTimeout) {
		mShiftLeg = -1;
		mSettleTime = 0.0f;
	}
}

bool RagdollBalance::NeedsClosingStep() const
{
	constexpr float32 scStanceWidthTolerance = 0.12f;
	constexpr float32 scStanceSplitTolerance = 0.2f;

	if (mStatus.Steps == 0 || !mLegs[0].bContact || !mLegs[1].bContact) {
		return false;
	}

	const JPH::Vec3 side = mGroundFrame.Up.Cross(mForward);
	const JPH::Vec3 spread = mGroundFrame.Horizontal(mLegs[0].FootCenter - mLegs[1].FootCenter);

	return (std::abs(std::abs(spread.Dot(side)) - mRestWidth) > scStanceWidthTolerance) ||
		   (std::abs(spread.Dot(mForward)) > scStanceSplitTolerance);
}

void RagdollBalance::BeginStumbleStep()
{
	// Added to a leg's weight share when choosing the catch step's foot, for stepping with the foot that just landed
	constexpr float32 scStumbleRepeatCost = 0.75f;

	const JPH::Vec3 drift = mGroundFrame.Horizontal(mStaggerVelocity);
	const JPH::Vec3 back = drift.NormalizedOr(JPH::Vec3::sZero());

	if (back.LengthSq() <= 0.0f) {
		return;
	}

	int32 swing_leg = -1;
	float32 best_cost = std::numeric_limits<float32>::max();

	for (uint32 leg = 0; leg < 2; leg++) {
		if (!mLegs[1 - leg].bContact) {
			continue;
		}

		// A light foot lifts easily
		float32 cost = GetWeightShare(leg);

		// The foot that just landed already took this step, so through a run of hits the feet take turns
		if (mStatus.Steps > 0 && mSwing.Leg == leg) {
			cost += scStumbleRepeatCost;
		}

		if (cost < best_cost) {
			best_cost = cost;
			swing_leg = static_cast<int32>(leg);
		}
	}

	if (swing_leg < 0) {
		return;
	}

	float32 swing_time = scMinSwingTime;
	const JPH::Vec3 landing = PlanStep(static_cast<uint32>(swing_leg), 1 - static_cast<uint32>(swing_leg), swing_time);

	StartSwing(static_cast<uint32>(swing_leg), landing, false, swing_time);
	mbStumbleStepped = true;
}

bool RagdollBalance::BeginStep()
{
	constexpr float32 scStanceDistanceCost = 2.0f;
	constexpr float32 scCaptureErrorCost = 10.0f;
	constexpr float32 scFreeFootPreference = 0.5f;
	constexpr float32 scMaxSwingWeight = 0.6f;
	constexpr float32 scMaxLoadWait = 0.2f;

	int32 best_leg = -1;
	float32 best_time = scMinSwingTime;
	float32 best_cost = std::numeric_limits<float32>::max();
	JPH::Vec3 best_landing = JPH::Vec3::sZero();

	for (uint32 swing = 0; swing < 2; swing++) {
		const uint32 stance = 1 - swing;

		if (!mLegs[stance].bContact) {
			continue;
		}

		// A foot carrying most of the weight cannot lift, so give the body a moment to move off it first
		if (GetWeightShare(swing) > scMaxSwingWeight && mLoadWaitTime < scMaxLoadWait) {
			continue;
		}

		float32 swing_time = scMinSwingTime;
		const JPH::Vec3 landing = PlanStep(swing, stance, swing_time);
		float32 cost = mGroundFrame.Horizontal(landing - mLegs[swing].FootCenter).Length() +
					   std::max(GetCaptureDistance(mLegs[stance]), 0.0f) * scStanceDistanceCost +
					   GetLandingCaptureError(stance, landing, swing_time) * scCaptureErrorCost;

		if (!mLegs[swing].bContact) {
			cost *= scFreeFootPreference;
		}

		if (cost < best_cost) {
			best_cost = cost;
			best_leg = static_cast<int32>(swing);
			best_landing = landing;
			best_time = swing_time;
		}
	}

	if (best_leg < 0) {
		return false;
	}

	StartSwing(static_cast<uint32>(best_leg), best_landing, false, best_time);
	mLoadWaitTime = 0.0f;

	return true;
}

JPH::Vec3 RagdollBalance::GetSwingDetour(const JPH::Vec3 start, const JPH::Vec3 end, float32 travel) const
{
	// How far a swinging foot passes from the stance ankle (m)
	constexpr float32 scSwingClearance = 0.2f;

	const JPH::Vec3 stance = mGroundFrame.Horizontal(mLegs[1 - mSwing.Leg].AnklePosition);
	const JPH::Vec3 path = end - start;
	const float32 length2 = path.LengthSq();

	if (length2 < 1e-4f) {
		return JPH::Vec3::sZero();
	}

	const float32 along = MathUtil::Clamp((stance - start).Dot(path) / length2, 0.0f, 1.0f);
	const JPH::Vec3 closest = start + path * along;
	const float32 distance = (closest - stance).Length();

	if (distance >= scSwingClearance) {
		return JPH::Vec3::sZero();
	}

	// Bend the path away from the stance leg on the side it already passes, or in front if it runs straight through
	JPH::Vec3 away = closest - stance;

	if (distance < 1e-3f) {
		away = mGroundFrame.Up.Cross(path);

		if (away.Dot(mForward) < 0.0f) {
			away = -away;
		}
	}

	return away.NormalizedOr(mForward) * ((scSwingClearance - distance) * std::sin(FX_PI * travel));
}

JPH::Vec3 RagdollBalance::PlanStep(uint32 swing_leg, uint32 stance_leg, float32& out_time) const
{
	constexpr float32 scSwingTimeStep = 0.04f;
	// Peak speed of a swinging foot (m/s)
	constexpr float32 scMaxSwingSpeed = 6.0f;

	const JPH::Vec3 swing_foot = mGroundFrame.Horizontal(mLegs[swing_leg].FootCenter);

	// The slowest swing that still lands where the body will need the foot. The capture point runs away while the foot
	// is in the air, so a longer swing needs a longer step
	for (float32 time = scMaxSwingTime; time > scMinSwingTime; time -= scSwingTimeStep) {
		const JPH::Vec3 unfitted = PredictCapture(stance_leg, time);
		const float32 reach = mGroundFrame.Horizontal(unfitted - mLegs[stance_leg].FootCenter).Length();
		const float32 travel = (mGroundFrame.Horizontal(unfitted) - swing_foot).Length();

		// Halved since a smooth swing's peak speed is about twice its mean
		if (reach <= scMaxStepLength && travel / time <= scMaxSwingSpeed * 0.5f) {
			out_time = time;
			return PlanLanding(swing_leg, stance_leg, time);
		}
	}

	out_time = scMinSwingTime;
	return PlanLanding(swing_leg, stance_leg, scMinSwingTime);
}

JPH::Vec3 RagdollBalance::PredictCapture(uint32 stance_leg, float32 time) const
{
	const Vec2f capture = mGroundFrame.ToPlane(mCapturePoint);
	const Vec2f pressure = GetStancePressure(mLegs[stance_leg], mGroundFrame, capture);
	const Vec2f predicted = ExtrapolateCapture(pressure, capture, std::exp(mOmega * time));

	return mGroundFrame.FromPlane(predicted, 0.0f);
}

bool RagdollBalance::IsViable() const
{
	// Slack on that reach, which assumes every step lands at once and exactly where it's aimed
	constexpr float32 scViabilityMargin = 2.0f;

	// How far the capture point can be from the feet and still be caught by a run of the longest, quickest steps
	const float32 shrink = std::exp(-mOmega * scMinSwingTime);
	const float32 reach = scMaxStepLength * shrink / (1.0f - shrink);

	return mCaptureOutside <= reach * scViabilityMargin;
}

void RagdollBalance::StartSwing(uint32 swing_leg, const JPH::Vec3 landing, bool fixed, float32 duration)
{
	BalanceLeg& leg = mLegs[swing_leg];
	leg.bStance = false;

	mSwing = Swing {};
	mSwing.Leg = swing_leg;
	mSwing.StartAnkle = leg.AnklePosition;
	mSwing.Landing = landing;
	mSwing.AnkleTarget = leg.AnklePosition;
	mSwing.PathPoint = leg.AnklePosition;
	mSwing.PathHeight = leg.AnklePosition.Dot(mGroundFrame.Up);
	mSwing.StartSurface = leg.SurfaceHeight;
	mSwing.bFixed = fixed;
	mSwing.Duration = duration;

	mStatus.State = eBalanceState::Stepping;
	mCalmTime = 0.0f;
	mSettleTime = 0.0f;
	mShiftLeg = -1;
}

float32 RagdollBalance::GetWeightShare(uint32 leg_index) const
{
	const BalanceLeg& leg = mLegs[leg_index];
	const BalanceLeg& other = mLegs[1 - leg_index];

	const bool is_off = !leg.bTouching && leg.SoleHeight - leg.SurfaceHeight > scNearGround;
	const bool other_off = !other.bTouching && other.SoleHeight - other.SurfaceHeight > scNearGround;

	if (is_off || other_off) {
		if (is_off) {
			return 0.0f;
		}

		return 1.0f;
	}

	// Where the centre of mass sits along the line between the feet: 1 is right over this foot
	const JPH::Vec3 own = mGroundFrame.Horizontal(leg.FootCenter);
	const JPH::Vec3 span = own - mGroundFrame.Horizontal(other.FootCenter);
	const float32 length2 = span.LengthSq();

	if (length2 < 1e-6f) {
		return 0.5f;
	}

	const float32 from_other = (mGroundFrame.Horizontal(mCenterOfMass) - mGroundFrame.Horizontal(other.FootCenter))
								   .Dot(span);

	return MathUtil::Clamp(from_other / length2, 0.0f, 1.0f);
}

float32 RagdollBalance::GetCaptureDistance(const BalanceLeg& leg) const
{
	Vec2f points[scSoleCorners];
	Vec2f hull[scSoleCorners + 1];

	// The whole sole, since this asks how far the capture point is from where this foot could bear weight
	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		points[corner] = mGroundFrame.ToPlane(leg.SoleWorld[corner]);
	}

	const uint32 count = BuildHull(points, scSoleCorners, hull);

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, mGroundFrame.ToPlane(mCapturePoint), distance);

	return distance;
}

float32 RagdollBalance::GetLandingCaptureError(uint32 stance_leg, const JPH::Vec3 landing, float32 swing_time) const
{
	constexpr float32 scCaptureMargin = 0.05f;

	const BalanceLeg& stance = mLegs[stance_leg];
	const JPH::Vec3 side = mGroundFrame.Up.Cross(mForward);

	Vec2f points[scSoleCorners * 2];
	Vec2f hull[scSoleCorners * 2 + 1];
	uint32 count = 0;

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		points[count++] = mGroundFrame.ToPlane(stance.SoleWorld[corner]);
	}

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		const JPH::Vec3 point = landing + side * (scSoleCornerSigns[corner][0] * mpRig->FootHalfWidth) +
								mForward * (scSoleCornerSigns[corner][1] * mpRig->FootHalfLength);
		points[count++] = mGroundFrame.ToPlane(point);
	}

	count = BuildHull(points, count, hull);

	const float32 growth = std::exp(mOmega * swing_time);
	const Vec2f capture = mGroundFrame.ToPlane(mCapturePoint);
	const Vec2f pressure = GetStancePressure(stance, mGroundFrame, capture);
	const Vec2f predicted = ExtrapolateCapture(pressure, capture, growth);

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, predicted, distance);

	return std::max(distance + scCaptureMargin, 0.0f);
}

JPH::Vec3 RagdollBalance::PlanLanding(uint32 swing_leg, uint32 stance_leg, float32 time_left) const
{
	constexpr float32 scLandingMargin = 0.08f;
	constexpr float32 scLandingSideMargin = 0.03f;
	// Extra outward reach on every step, so the stance opens up while the body is off balance
	constexpr float32 scStanceWidenMargin = 0.1f;

	// The leg that is currently holding up the body
	const BalanceLeg& stance = mLegs[stance_leg];
	// The leg that is supposed to swing and land
	const BalanceLeg& swing = mLegs[swing_leg];

	// The capture point on the ground plane, the spot the body has to be balanced over
	const Vec2f capture = mGroundFrame.ToPlane(mCapturePoint);
	const Vec2f pressure = GetStancePressure(stance, mGroundFrame, capture);

	// Try and predict where the foot will be on contact with the ground
	const float32 growth = std::exp(mOmega * std::max(time_left, 0.0f));
	const Vec2f predicted = ExtrapolateCapture(pressure, capture, growth);
	// The offset from the pressure point to the predicted capture point
	const Vec2f pressure_point_error = (predicted - pressure);

	// Get the world direction. With no error, fall back to horizontal velocity.
	JPH::Vec3 direction = mGroundFrame.FromPlane(pressure_point_error, 0.0f);
	direction = direction.NormalizedOr(mGroundFrame.Horizontal(mVelocity).NormalizedOr(mLeanDirection));

	const JPH::Vec3 side = mGroundFrame.Up.Cross(mForward) * swing.SideSign;

	// Aim at the predicted capture point
	// Fades with the error it follows, whose direction spins round as the error passes through nothing
	const float32 margin = std::min(pressure_point_error.Length(), scLandingMargin);

	const JPH::Vec3 landing_point = mGroundFrame.FromPlane(predicted, 0.0f) + direction * margin +
									side * (scLandingSideMargin + scStanceWidenMargin);

	return FitLanding(swing_leg, stance_leg, landing_point);
}

JPH::Vec3 RagdollBalance::FitLanding(uint32 swing_leg, uint32 stance_leg, JPH::Vec3 landing) const
{
	constexpr float32 scMinStepWidth = 0.22f;
	constexpr float32 scMaxStepWidth = 0.6f;
	// How far past the stance foot a crossover step can land (m), and how far in front or behind it to pass the other
	// leg
	constexpr float32 scMaxCrossover = 0.3f;
	constexpr float32 scCrossoverClearance = 0.2f;
	// Lateral offset (m) under which a landing counts as being in line with the stance foot
	constexpr float32 scCrossoverLateral = 0.1f;

	const JPH::Vec3 side = mGroundFrame.Up.Cross(mForward) * mLegs[swing_leg].SideSign;

	JPH::Vec3 landing_point = mGroundFrame.Horizontal(landing);

	// The stance foot's centre on the ground, which every step is measured from
	const JPH::Vec3 stance_center = mGroundFrame.Horizontal(mLegs[stance_leg].FootCenter);
	// How far the landing sits out to the swing side of the stance foot
	const float32 lateral = (landing_point - stance_center).Dot(side);

	// A landing inside the stance foot is a crossover, the only way the trailing foot can gain ground in a sideways
	// shove. It lands in front of or behind the stance foot, on whichever side the swing foot already is, to get past
	// the leg
	if (lateral < scMinStepWidth) {
		const float32 fore = (landing_point - stance_center).Dot(mForward);

		if (lateral < -scMaxCrossover) {
			landing_point += side * (-scMaxCrossover - lateral);
		}

		if (lateral < scCrossoverLateral && std::abs(fore) < scCrossoverClearance) {
			float32 fore_side = 1.0f;

			if (mGroundFrame.Horizontal(mLegs[swing_leg].FootCenter - stance_center).Dot(mForward) < 0.0f) {
				fore_side = -1.0f;
			}

			landing_point += mForward * (fore_side * scCrossoverClearance - fore);
		}
	}
	else if (lateral > scMaxStepWidth) {
		landing_point -= side * (lateral - scMaxStepWidth);
	}

	JPH::Vec3 step = landing_point - stance_center;
	const float32 step_length = step.Length();

	// Check to make sure that a step isn't too wide. If it is, shorten it.
	if (step_length > scMaxStepLength) {
		step *= scMaxStepLength / step_length;
	}

	return ToLanding(stance_center + step);
}

JPH::Vec3 RagdollBalance::GetHeadingForward(const JPH::Quat& orientation, const JPH::Quat& rest) const
{
	return mGroundFrame.Horizontal((orientation * rest.Conjugated()) * mpRig->RestForward)
		.NormalizedOr(mpRig->RestForward);
}

JPH::Quat RagdollBalance::GetHeadingRotation(const JPH::Vec3 forward) const
{
	const JPH::Vec3 heading = mGroundFrame.Horizontal(forward).NormalizedOr(mpRig->RestForward);
	const float32 angle = std::atan2(mpRig->RestForward.Cross(heading).Dot(mGroundFrame.Up),
									 mpRig->RestForward.Dot(heading));

	return JPH::Quat::sRotation(mGroundFrame.Up, angle);
}

JPH::Vec3 RagdollBalance::GetLandingAnkle(const BalanceLeg& leg, const JPH::Vec3 landing) const
{
	const JPH::Quat landing_foot = GetHeadingRotation(mForward) * leg.RestFoot;

	return landing - landing_foot * leg.SoleCenter;
}

JPH::Vec3 RagdollBalance::ToGround(const JPH::Vec3 point) const
{
	return mGroundFrame.Horizontal(point) + mGroundFrame.Up * mGroundHeight;
}

JPH::Vec3 RagdollBalance::ToLanding(const JPH::Vec3 point) const
{
	const JPH::Vec3 along_sole = mForward * mpRig->FootHalfLength;

	const float32 height = std::max(
		{ GetGroundAt(point), GetGroundAt(point + along_sole), GetGroundAt(point - along_sole) });

	return mGroundFrame.Horizontal(point) + mGroundFrame.Up * height;
}

bool RagdollBalance::ProbeSurface(const JPH::Vec3 point, float32& out_height) const
{
	// How far below the centre of mass the ground probes reach (m)
	constexpr float32 scProbeLength = 3.0f;

	const JPH::Vec3 origin = mGroundFrame.Horizontal(point) + mGroundFrame.Up * mCenterOfMass.Dot(mGroundFrame.Up);
	const RayResult hit = gPhysics->pBackend->RaycastStaticNoLock(FVec3::FromJolt(origin),
																  FVec3::FromJolt(-mGroundFrame.Up * scProbeLength));

	if (!hit.bHit || FVec3::ToJolt(hit.Normal).Dot(mGroundFrame.Up) < scSupportNormalMinUp) {
		return false;
	}

	out_height = FVec3::ToJolt(hit.Point).Dot(mGroundFrame.Up);

	return true;
}

float32 RagdollBalance::GetGroundAt(const JPH::Vec3 point) const
{
	// A probed surface this close to the ground the feet are on is that ground (m)
	constexpr float32 scSameSurface = 0.01f;

	float32 probe = 0.0f;

	if (!mbHasGroundProbe || !ProbeSurface(point, probe)) {
		return mGroundHeight;
	}

	// Measured from the probe under the planted foot rather than taken as is, since the feet stand a little way into
	// what they stand on
	const float32 difference = probe - mGroundProbe;

	if (std::abs(difference) < scSameSurface) {
		return mGroundHeight;
	}

	return mGroundHeight + difference;
}

} // namespace fx::physics
