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
constexpr float32 scSupportShrink = 0.5f;
constexpr float32 scCentering = 0.2f;

constexpr float32 scStepTrigger = 0.02f;
constexpr float32 scMinDoubleSupport = 0.05f;
constexpr float32 scSwingTime = 0.3f;
constexpr float32 scSwingFreezeFraction = 0.75f;
constexpr float32 scSwingTravelFraction = 0.85f;
constexpr float32 scSwingLift = 0.07f;
constexpr float32 scSwingOvertime = 0.15f;
constexpr float32 scSwingMinTouchdown = 0.6f;
constexpr float32 scTouchdownDistance = 0.05f;
constexpr float32 scLandingMargin = 0.05f;
constexpr float32 scLandingSideMargin = 0.03f;
constexpr float32 scMinStepWidth = 0.12f;
constexpr float32 scMaxStepWidth = 0.5f;
constexpr float32 scMaxStepLength = 0.75f;
constexpr float32 scStanceDistanceCost = 2.0f;
constexpr float32 scCaptureErrorCost = 10.0f;
constexpr float32 scCaptureMargin = 0.05f;
constexpr float32 scFreeFootPreference = 0.5f;
constexpr uint32 scMaxRecoverySteps = 6;

constexpr float32 scFallTilt = MathUtil::DegToRad(50.0f);
constexpr float32 scFallHeightFraction = 0.6f;
constexpr float32 scCalmSpeed = 0.08f;
constexpr float32 scCalmTime = 0.5f;

constexpr float32 scSettleSpeed = 0.25f;
constexpr float32 scSettleTime = 0.3f;
constexpr float32 scSettleInside = 0.03f;
constexpr float32 scStanceWidthTolerance = 0.12f;
constexpr float32 scStanceSplitTolerance = 0.2f;
constexpr float32 scShiftInside = 0.02f;
constexpr float32 scShiftSpeed = 0.3f;
constexpr float32 scShiftTimeout = 1.0f;

} // namespace

bool RagdollBalance::Init(Ragdoll& ragdoll)
{
	mbValid = false;
	mpRig = &ragdoll.GetRig();

	if (!mpRig->bCanBalance) {
		return false;
	}

	mFrame = GravityFrame::FromPhysics();

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
	const SizedArray<Ragdoll::BodyInfo>& bodies = ragdoll.mBodies;
	const Ragdoll::BodyInfo& pelvis = bodies[0];

	mRestPelvis = pelvis.RestOrientation;

	const JPH::Vec3 side = mFrame.Up.Cross(mpRig->RestForward);
	float32 rest_ground = std::numeric_limits<float32>::max();

	for (const BalanceLeg& leg : mLegs) {
		const Ragdoll::BodyInfo& foot = bodies[leg.Foot];

		for (const JPH::Vec3& corner : leg.SoleCorners) {
			rest_ground = std::min(rest_ground, (foot.RestPosition + foot.RestOrientation * corner).Dot(mFrame.Up));
		}
	}

	const JPH::Vec3 rest_foot_a = bodies[mLegs[0].Foot].RestPosition +
								  bodies[mLegs[0].Foot].RestOrientation * mLegs[0].SoleCenter;
	const JPH::Vec3 rest_foot_b = bodies[mLegs[1].Foot].RestPosition +
								  bodies[mLegs[1].Foot].RestOrientation * mLegs[1].SoleCenter;

	mRestWidth = std::abs((rest_foot_a - rest_foot_b).Dot(side));

	JPH::Vec3 rest_com = JPH::Vec3::sZero();
	const float32 total_mass = ragdoll.GetTotalMass();

	for (const Ragdoll::BodyInfo& body : bodies) {
		rest_com += (body.RestPosition + body.RestOrientation * body.LocalCenter) * body.Mass;
	}

	if (total_mass <= 0.0f) {
		return false;
	}

	mRestComHeight = std::max((rest_com / total_mass).Dot(mFrame.Up) - rest_ground, scMinPendulumHeight);

	return true;
}

void RagdollBalance::Reset()
{
	for (BalanceLeg& leg : mLegs) {
		leg.bStance = true;
		leg.bContact = false;
		leg.bTouching = false;
		leg.ContactAge = scContactHoldTime;
		leg.ContactCornerCount = 0;
	}

	mSwing = Swing {};
	mDoubleSupportTime = 0.0f;
	mCalmTime = 0.0f;
	mSettleTime = 0.0f;
	mShiftTime = 0.0f;
	mShiftLeg = -1;
	mbBodyOnGround = false;
	mSupportCount = 0;
	mbHasGround = false;
	mStatus = BalanceStatus {};

	if (mbValid) {
		mStatus.State = eBalanceState::Standing;
	}
}

void RagdollBalance::Clear(Ragdoll& ragdoll)
{
	ragdoll.ClearMotorOverrides();

	if (mStatus.State != eBalanceState::Fallen) {
		mStatus.State = eBalanceState::Off;
	}
}

void RagdollBalance::Step(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time,
						  const JPH::Vec3 gravity, bool contacts_fresh)
{
	ragdoll.ClearMotorOverrides();

	if (!mbValid || mStatus.State == eBalanceState::Off || mStatus.State == eBalanceState::Fallen) {
		return;
	}

	for (const Ragdoll::BodyInfo& body : ragdoll.mBodies) {
		if (body.Mode != eRagdollBodyMode::Powered) {
			return;
		}
	}

	GatherState(ragdoll, delta_time, gravity, contacts_fresh);
	BuildSupport();

	if (!DetectFall()) {
		if (mStatus.State == eBalanceState::Standing) {
			UpdateStanding(delta_time);
		}
		else if (mStatus.State == eBalanceState::Stepping) {
			UpdateStepping(delta_time);
		}
	}

	mStatus.bCalm = (mStatus.State == eBalanceState::Standing) && (mCalmTime > scCalmTime) && (mShiftLeg < 0);

	if (mStatus.State == eBalanceState::Fallen) {
		return;
	}

	DriveStanceLegs(ragdoll);

	if (mStatus.State == eBalanceState::Stepping) {
		DriveSwingLeg(ragdoll);
	}

	ApplyTorsoControl(ragdoll, body_interface, gravity);
}

void RagdollBalance::GatherState(Ragdoll& ragdoll, float32 delta_time, const JPH::Vec3 gravity, bool contacts_fresh)
{
	mFrame = GravityFrame::FromGravity(gravity);

	ragdoll.GetMassAverage(ragdoll.mStates, mCenterOfMass, mVelocity);

	mPelvisOrientation = ragdoll.mStates[0].Orientation;
	mForward = GetHeadingForward(ragdoll.mTargets[0].Orientation, mRestPelvis);

	GatherFeet(ragdoll, delta_time, contacts_fresh);

	if (contacts_fresh) {
		mbBodyOnGround = IsAnyBodyOnGround(ragdoll);
	}

	const float32 height = std::max(mCenterOfMass.Dot(mFrame.Up) - mGroundHeight, scMinPendulumHeight);
	mOmega = std::sqrt(mFrame.Gravity / height);
	mCapturePoint = ToGround(mCenterOfMass + mFrame.Horizontal(mVelocity) / mOmega);

	const JPH::Quat chest = ragdoll.mStates[mpRig->Chest].Orientation;
	const JPH::Vec3 chest_up = (chest * ragdoll.mBodies[mpRig->Chest].RestOrientation.Conjugated()) * mFrame.Up;

	mTrunkTilt = std::acos(MathUtil::Clamp(chest_up.Dot(mFrame.Up), -1.0f, 1.0f));
}

void RagdollBalance::GatherFeet(Ragdoll& ragdoll, float32 delta_time, bool contacts_fresh)
{
	float32 contact_ground = std::numeric_limits<float32>::max();

	for (BalanceLeg& leg : mLegs) {
		const Ragdoll::BodyState& foot = ragdoll.mStates[leg.Foot];
		const JPH::Quat orientation = foot.Orientation;

		leg.AnklePosition = foot.Origin;
		leg.FootOrientation = orientation;
		leg.FootCenter = leg.AnklePosition + orientation * leg.SoleCenter;

		leg.ContactAge += delta_time;

		if (contacts_fresh) {
			leg.bTouching = ragdoll.IsBodyOnGround(leg.Foot, mFrame.Up, scSupportNormalMinUp);

			if (leg.bTouching) {
				leg.ContactAge = 0.0f;
			}
		}

		leg.SoleHeight = std::numeric_limits<float32>::max();

		for (uint32 corner = 0; corner < scSoleCorners; corner++) {
			leg.ContactCorners[corner] = leg.AnklePosition + orientation * leg.SoleCorners[corner];
			leg.SoleHeight = std::min(leg.SoleHeight, leg.ContactCorners[corner].Dot(mFrame.Up));
		}

		if (leg.ContactAge < scContactHoldTime) {
			contact_ground = std::min(contact_ground, leg.SoleHeight);
		}
	}

	if (contact_ground < std::numeric_limits<float32>::max()) {
		mGroundHeight = contact_ground;
		mbHasGround = true;
	}
	else if (!mbHasGround) {
		mGroundHeight = std::min(mLegs[0].FootCenter.Dot(mFrame.Up), mLegs[1].FootCenter.Dot(mFrame.Up));
	}

	for (BalanceLeg& leg : mLegs) {
		leg.bContact = (leg.ContactAge < scContactHoldTime) ||
					   (mbHasGround && leg.SoleHeight - mGroundHeight < scNearGround);
		leg.ContactCornerCount = 0;

		if (leg.bContact) {
			leg.ContactCornerCount = scSoleCorners;
		}
	}
}

bool RagdollBalance::IsAnyBodyOnGround(Ragdoll& ragdoll) const
{
	for (uint32 i = 0; i < ragdoll.mBodies.Size; i++) {
		if (ragdoll.mBodies[i].Part == eRagdollBodyPart::Leg && i != mLegs[0].Thigh && i != mLegs[1].Thigh) {
			continue;
		}

		if (ragdoll.IsBodyOnGround(i, mFrame.Up, scSupportNormalMinUp)) {
			return true;
		}
	}

	return false;
}

void RagdollBalance::BuildSupport()
{
	constexpr uint32 scMaxPoints = 2 * scSoleCorners;

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

		for (uint32 corner = 0; corner < leg.ContactCornerCount; corner++) {
			points[count++] = mFrame.ToPlane(leg.ContactCorners[corner]);
		}
	}

	Vec2f hull[scMaxPoints + 1];
	mSupportCount = BuildHull(points, count, hull);

	if (mSupportCount == 0) {
		mSupportCenter = ToGround(foot_center_sum / static_cast<float32>(std::max(stance_count, 1u)));
		mSupportTarget = mSupportCenter;
		mCaptureOutside = 0.0f;
		return;
	}

	const Vec2f center = GetHullCentroid(hull, mSupportCount);

	mSupportCenter = mFrame.FromPlane(center, mGroundHeight);

	ClosestOnHull(hull, mSupportCount, mFrame.ToPlane(mCapturePoint), mCaptureOutside);

	Vec2f shrunk[scMaxPoints + 1];

	for (uint32 i = 0; i < mSupportCount; i++) {
		shrunk[i] = center + (hull[i] - center) * scSupportShrink;
	}

	float32 unused_distance = 0.0f;
	const Vec2f held = ClosestOnHull(shrunk, mSupportCount, mFrame.ToPlane(mCenterOfMass), unused_distance);
	const Vec2f target = held + (center - held) * scCentering;

	mSupportTarget = mFrame.FromPlane(target, mGroundHeight);
}

bool RagdollBalance::DetectFall()
{
	eFallReason reason = eFallReason::None;

	if (mTrunkTilt > scFallTilt) {
		reason = eFallReason::Tilt;
	}
	else if (mCenterOfMass.Dot(mFrame.Up) - mGroundHeight < mRestComHeight * scFallHeightFraction) {
		reason = eFallReason::Height;
	}
	else if (mbBodyOnGround) {
		reason = eFallReason::Contact;
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
	mDoubleSupportTime += delta_time;

	if (mCaptureOutside < 0.0f && mFrame.Horizontal(mVelocity).Length() < scCalmSpeed) {
		mCalmTime += delta_time;
	}
	else {
		mCalmTime = 0.0f;
	}

	if (mCalmTime > scCalmTime) {
		mStatus.bRecovering = false;
		mStatus.RecoverySteps = 0;
	}

	if (mSupportCount > 0 && mCaptureOutside > scStepTrigger && mDoubleSupportTime > scMinDoubleSupport) {
		mShiftLeg = -1;
		BeginStep();
		return;
	}

	UpdateClosingStep(delta_time);
}

void RagdollBalance::UpdateStepping(float32 delta_time)
{
	BalanceLeg& leg = mLegs[mSwing.Leg];

	mSwing.Time += delta_time;

	const float32 phase = mSwing.Time / scSwingTime;

	if (!mSwing.bFixed && phase < scSwingFreezeFraction) {
		mSwing.Landing = PlanLanding(mSwing.Leg, 1 - mSwing.Leg, scSwingTime - mSwing.Time);
	}

	const JPH::Quat landing_foot = GetHeadingRotation(mForward) * leg.RestFoot;
	const JPH::Vec3 landing_ankle = mSwing.Landing - landing_foot * leg.SoleCenter;

	const float32 travel = MathUtil::SmoothStep(phase / scSwingTravelFraction);
	const JPH::Vec3 start = mFrame.Horizontal(mSwing.StartAnkle);
	const JPH::Vec3 end = mFrame.Horizontal(landing_ankle);

	float32 height = mSwing.StartAnkle.Dot(mFrame.Up) +
					 (landing_ankle.Dot(mFrame.Up) - mSwing.StartAnkle.Dot(mFrame.Up)) * travel;

	if (phase < 1.0f) {
		height += scSwingLift * std::sin(FX_PI * phase);
	}
	else {
		height -= scSwingPress;
	}

	mSwing.AnkleTarget = start + (end - start) * travel + mFrame.Up * height;

	if (!leg.bTouching) {
		mSwing.bLifted = true;
	}

	const bool near_landing = mFrame.Horizontal(leg.AnklePosition - landing_ankle).Length() < scTouchdownDistance;
	const bool touched_down = leg.bTouching &&
							  ((mSwing.bLifted && (phase > scSwingMinTouchdown || near_landing)) || phase >= 1.0f);

	if (!touched_down && mSwing.Time <= scSwingTime + scSwingOvertime) {
		return;
	}

	leg.bStance = true;

	mStatus.State = eBalanceState::Standing;
	mStatus.Steps++;
	mDoubleSupportTime = 0.0f;

	if (!mSwing.bFixed) {
		mStatus.RecoverySteps++;
	}
}

void RagdollBalance::UpdateClosingStep(float32 delta_time)
{
	if (mCaptureOutside < -scSettleInside && mFrame.Horizontal(mVelocity).Length() < scSettleSpeed) {
		mSettleTime += delta_time;
	}
	else {
		mSettleTime = 0.0f;
	}

	if (mShiftLeg < 0) {
		if (mSettleTime > scSettleTime && NeedsClosingStep()) {
			const float32 distance_a = mFrame.Horizontal(mCenterOfMass - mLegs[0].FootCenter).Length();
			const float32 distance_b = mFrame.Horizontal(mCenterOfMass - mLegs[1].FootCenter).Length();

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

	if (GetCaptureDistance(stance) < -scShiftInside && mFrame.Horizontal(mVelocity).Length() < scShiftSpeed) {
		const JPH::Vec3 side = mFrame.Up.Cross(mForward) * mLegs[swing_leg].SideSign;
		StartSwing(swing_leg, ToGround(stance.FootCenter + side * mRestWidth), true);
		return;
	}

	if (mShiftTime > scShiftTimeout) {
		mShiftLeg = -1;
		mSettleTime = 0.0f;
	}
}

bool RagdollBalance::NeedsClosingStep() const
{
	if (mStatus.Steps == 0 || !mLegs[0].bContact || !mLegs[1].bContact) {
		return false;
	}

	const JPH::Vec3 side = mFrame.Up.Cross(mForward);
	const JPH::Vec3 spread = mFrame.Horizontal(mLegs[0].FootCenter - mLegs[1].FootCenter);

	return (std::abs(std::abs(spread.Dot(side)) - mRestWidth) > scStanceWidthTolerance) ||
		   (std::abs(spread.Dot(mForward)) > scStanceSplitTolerance);
}

bool RagdollBalance::BeginStep()
{
	if (mStatus.RecoverySteps >= scMaxRecoverySteps) {
		Fall(eFallReason::StepBudget);
		return false;
	}

	int32 best_leg = -1;
	float32 best_cost = std::numeric_limits<float32>::max();
	JPH::Vec3 best_landing = JPH::Vec3::sZero();

	for (uint32 swing = 0; swing < 2; swing++) {
		const uint32 stance = 1 - swing;

		if (!mLegs[stance].bContact) {
			continue;
		}

		const JPH::Vec3 landing = PlanLanding(swing, stance, scSwingTime);
		float32 cost = mFrame.Horizontal(landing - mLegs[swing].FootCenter).Length() +
					   std::max(GetCaptureDistance(mLegs[stance]), 0.0f) * scStanceDistanceCost +
					   GetLandingCaptureError(stance, landing) * scCaptureErrorCost;

		if (!mLegs[swing].bContact) {
			cost *= scFreeFootPreference;
		}

		if (cost < best_cost) {
			best_cost = cost;
			best_leg = static_cast<int32>(swing);
			best_landing = landing;
		}
	}

	if (best_leg < 0) {
		return false;
	}

	StartSwing(static_cast<uint32>(best_leg), best_landing, false);
	mStatus.bRecovering = true;

	return true;
}

void RagdollBalance::StartSwing(uint32 swing_leg, const JPH::Vec3 landing, bool fixed)
{
	BalanceLeg& leg = mLegs[swing_leg];
	leg.bStance = false;

	mSwing = Swing {};
	mSwing.Leg = swing_leg;
	mSwing.StartAnkle = leg.AnklePosition;
	mSwing.Landing = landing;
	mSwing.AnkleTarget = leg.AnklePosition;
	mSwing.bFixed = fixed;

	mStatus.State = eBalanceState::Stepping;
	mCalmTime = 0.0f;
	mSettleTime = 0.0f;
	mShiftLeg = -1;
}

float32 RagdollBalance::GetCaptureDistance(const BalanceLeg& leg) const
{
	Vec2f points[scSoleCorners];
	Vec2f hull[scSoleCorners + 1];

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		points[corner] = mFrame.ToPlane(leg.ContactCorners[corner]);
	}

	const uint32 count = BuildHull(points, scSoleCorners, hull);

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, mFrame.ToPlane(mCapturePoint), distance);

	return distance;
}

float32 RagdollBalance::GetLandingCaptureError(uint32 stance_leg, const JPH::Vec3 landing) const
{
	const BalanceLeg& stance = mLegs[stance_leg];
	const JPH::Vec3 side = mFrame.Up.Cross(mForward);

	Vec2f points[scSoleCorners * 2];
	Vec2f hull[scSoleCorners * 2 + 1];
	uint32 count = 0;

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		points[count++] = mFrame.ToPlane(stance.ContactCorners[corner]);
	}

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		const JPH::Vec3 point = landing + side * (scSoleCornerSigns[corner][0] * mpRig->FootHalfWidth) +
								mForward * (scSoleCornerSigns[corner][1] * mpRig->FootHalfLength);
		points[count++] = mFrame.ToPlane(point);
	}

	count = BuildHull(points, count, hull);

	const float32 growth = std::exp(mOmega * scSwingTime);
	const JPH::Vec3 pressure = stance.FootCenter;
	const JPH::Vec3 predicted = pressure + mFrame.Horizontal(mCapturePoint - pressure) * growth;

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, mFrame.ToPlane(predicted), distance);

	return std::max(distance + scCaptureMargin, 0.0f);
}

JPH::Vec3 RagdollBalance::PlanLanding(uint32 swing_leg, uint32 stance_leg, float32 time_left) const
{
	const BalanceLeg& stance = mLegs[stance_leg];
	const BalanceLeg& swing = mLegs[swing_leg];

	Vec2f stance_points[scSoleCorners];
	Vec2f stance_hull[scSoleCorners + 1];
	uint32 stance_count = 0;

	for (uint32 corner = 0; corner < stance.ContactCornerCount; corner++) {
		stance_points[stance_count++] = mFrame.ToPlane(stance.ContactCorners[corner]);
	}

	if (stance_count == 0) {
		stance_points[stance_count++] = mFrame.ToPlane(stance.FootCenter);
	}

	stance_count = BuildHull(stance_points, stance_count, stance_hull);

	const Vec2f capture = mFrame.ToPlane(mCapturePoint);

	float32 unused_distance = 0.0f;
	const Vec2f pressure = ClosestOnHull(stance_hull, stance_count, capture, unused_distance);

	const float32 growth = std::exp(mOmega * std::max(time_left, 0.0f));
	const Vec2f predicted = pressure + (capture - pressure) * growth;

	JPH::Vec3 direction = mFrame.AxisU * (predicted.X - pressure.X) + mFrame.AxisV * (predicted.Y - pressure.Y);
	direction = direction.NormalizedOr(mFrame.Horizontal(mVelocity).NormalizedOr(mForward));

	const JPH::Vec3 side = mFrame.Up.Cross(mForward) * swing.SideSign;
	JPH::Vec3 landing = mFrame.AxisU * predicted.X + mFrame.AxisV * predicted.Y + direction * scLandingMargin +
						side * scLandingSideMargin;

	const JPH::Vec3 stance_center = mFrame.Horizontal(stance.FootCenter);
	const float32 lateral = (landing - stance_center).Dot(side);

	if (lateral < scMinStepWidth) {
		landing += side * (scMinStepWidth - lateral);
	}
	else if (lateral > scMaxStepWidth) {
		landing -= side * (lateral - scMaxStepWidth);
	}

	JPH::Vec3 step = landing - stance_center;
	const float32 step_length = step.Length();

	if (step_length > scMaxStepLength) {
		step *= scMaxStepLength / step_length;
	}

	return ToGround(stance_center + step);
}

JPH::Vec3 RagdollBalance::GetHeadingForward(const JPH::Quat& orientation, const JPH::Quat& rest) const
{
	return mFrame.Horizontal((orientation * rest.Conjugated()) * mpRig->RestForward).NormalizedOr(mpRig->RestForward);
}

JPH::Quat RagdollBalance::GetHeadingRotation(const JPH::Vec3 forward) const
{
	const JPH::Vec3 heading = mFrame.Horizontal(forward).NormalizedOr(mpRig->RestForward);
	const float32 angle = std::atan2(mpRig->RestForward.Cross(heading).Dot(mFrame.Up), mpRig->RestForward.Dot(heading));

	return JPH::Quat::sRotation(mFrame.Up, angle);
}

JPH::Vec3 RagdollBalance::ToGround(const JPH::Vec3 point) const
{
	return mFrame.Horizontal(point) + mFrame.Up * mGroundHeight;
}

} // namespace fx::physics
