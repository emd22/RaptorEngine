#include "RagdollBalance.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"
#include "Ragdoll.hpp"

#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
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

	if (ragdoll.mBodies.Size < 2) {
		return false;
	}

	const JPH::Vec3 gravity = gPhysics->pBackend->PhysicsSystem.GetGravity();
	mUp = GetUpFromGravity(gravity);
	mGravity = gravity.Length();

	if (!InitLegs(ragdoll) || !InitRestPose(ragdoll)) {
		return false;
	}

	mbValid = true;
	Reset();

	return true;
}

bool RagdollBalance::InitLegs(const Ragdoll& ragdoll)
{
	const SizedArray<Ragdoll::BodyInfo>& bodies = ragdoll.mBodies;

	uint32 leg_count = 0;
	mNeckBody = 0;

	JPH::Vec3 forward = JPH::Vec3::sZero();

	for (uint32 i = 0; i < bodies.Size; i++) {
		const Ragdoll::BodyInfo& foot = bodies[i];

		if (foot.Part == eRagdollBodyPart::Head && mNeckBody == 0) {
			mNeckBody = i;
		}

		if (!foot.bFoot || !foot.bBox || foot.ParentBody < 0) {
			continue;
		}

		const int32 shin = foot.ParentBody;
		const int32 thigh = bodies[shin].ParentBody;

		if (thigh <= 0 || bodies[thigh].ParentBody != 0 || !bodies[shin].bHinge) {
			continue;
		}

		RagdollLeg& leg = mLegs[leg_count++];

		leg = RagdollLeg {};
		leg.Thigh = static_cast<uint32>(thigh);
		leg.Shin = static_cast<uint32>(shin);
		leg.Foot = i;
		leg.Upper = (bodies[shin].RestPosition - bodies[thigh].RestPosition).Length();
		leg.Lower = (foot.RestPosition - bodies[shin].RestPosition).Length();
		leg.HipOffset = bodies[thigh].ParentAnchor;
		leg.RestFoot = foot.RestOrientation;

		const JPH::Vec3 half = foot.BoxHalfExtent;

		for (uint32 corner = 0; corner < scSoleCorners; corner++) {
			leg.SoleCorners[corner] = foot.LocalCenter +
									  foot.BoxRotation * JPH::Vec3(scSoleCornerSigns[corner][0] * half.GetX(),
																   -half.GetY(),
																   scSoleCornerSigns[corner][1] * half.GetZ());
		}

		leg.SoleCenter = foot.LocalCenter + foot.BoxRotation * JPH::Vec3(0.0f, -half.GetY(), 0.0f);

		mFootHalfWidth = half.GetX();
		mFootHalfLength = half.GetZ();

		forward += foot.RestOrientation * foot.BoxRotation * JPH::Vec3::sAxisZ();
	}

	if (leg_count != 2) {
		LogError("RagdollBalance: Leg count is not 2");
		return false;
	};

	if (mNeckBody == 0 || bodies[mNeckBody].ParentBody < 0) {
		return false;
	}

	mChestBody = static_cast<uint32>(bodies[mNeckBody].ParentBody);
	mRestForward = Horizontal(forward).NormalizedOr(JPH::Vec3::sAxisZ());
	mForward = mRestForward;

	return true;
}

bool RagdollBalance::InitRestPose(const Ragdoll& ragdoll)
{
	const SizedArray<Ragdoll::BodyInfo>& bodies = ragdoll.mBodies;
	const Ragdoll::BodyInfo& pelvis = bodies[0];

	mRestPelvis = pelvis.RestOrientation;

	const JPH::Vec3 side = mUp.Cross(mRestForward);
	float32 rest_ground = std::numeric_limits<float32>::max();

	for (RagdollLeg& leg : mLegs) {
		const Ragdoll::BodyInfo& thigh = bodies[leg.Thigh];
		const Ragdoll::BodyInfo& shin = bodies[leg.Shin];
		const Ragdoll::BodyInfo& foot = bodies[leg.Foot];

		const JPH::Vec3 leg_direction = (foot.RestPosition - thigh.RestPosition).Normalized();
		const JPH::Vec3 rest_normal = leg_direction.Cross(mRestForward);
		const JPH::Vec3 rest_hinge = shin.RestOrientation * shin.HingeLocal;

		leg.HingeSign = (rest_hinge.Dot(rest_normal) >= 0.0f) ? 1.0f : -1.0f;
		leg.SideSign = ((thigh.RestPosition - pelvis.RestPosition).Dot(side) >= 0.0f) ? 1.0f : -1.0f;

		for (const JPH::Vec3& corner : leg.SoleCorners) {
			rest_ground = std::min(rest_ground, (foot.RestPosition + foot.RestOrientation * corner).Dot(mUp));
		}
	}

	if (mLegs[0].SideSign == mLegs[1].SideSign) {
		return false;
	}

	const JPH::Vec3 rest_foot_a = bodies[mLegs[0].Foot].RestPosition +
								  bodies[mLegs[0].Foot].RestOrientation * mLegs[0].SoleCenter;
	const JPH::Vec3 rest_foot_b = bodies[mLegs[1].Foot].RestPosition +
								  bodies[mLegs[1].Foot].RestOrientation * mLegs[1].SoleCenter;

	mRestWidth = std::abs((rest_foot_a - rest_foot_b).Dot(side));

	JPH::Vec3 rest_com = JPH::Vec3::sZero();
	float32 total_mass = 0.0f;

	for (const Ragdoll::BodyInfo& body : bodies) {
		rest_com += (body.RestPosition + body.RestOrientation * body.LocalCenter) * body.Mass;
		total_mass += body.Mass;
	}

	if (total_mass <= 0.0f) {
		return false;
	}

	mRestComHeight = std::max((rest_com / total_mass).Dot(mUp) - rest_ground, scMinPendulumHeight);

	return true;
}

void RagdollBalance::Reset()
{
	for (RagdollLeg& leg : mLegs) {
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
	mStatus.State = mbValid ? eBalanceState::Standing : eBalanceState::Off;
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

	GatherState(ragdoll, body_interface, delta_time, gravity, contacts_fresh);
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

void RagdollBalance::GatherState(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time,
								 const JPH::Vec3 gravity, bool contacts_fresh)
{
	mGravity = gravity.Length();
	mUp = GetUpFromGravity(gravity);
	mAxisU = mUp.GetNormalizedPerpendicular();
	mAxisV = mUp.Cross(mAxisU);

	float32 total_mass = 0.0f;
	JPH::Vec3 weighted_center = JPH::Vec3::sZero();
	JPH::Vec3 momentum = JPH::Vec3::sZero();

	for (uint32 i = 0; i < ragdoll.mBodies.Size; i++) {
		const float32 mass = ragdoll.mBodies[i].Mass;

		weighted_center += ragdoll.mStates[i].Center * mass;
		momentum += body_interface.GetLinearVelocity(ragdoll.GetBodyID(i)) * mass;
		total_mass += mass;
	}

	mCenterOfMass = weighted_center / total_mass;
	mVelocity = momentum / total_mass;

	mPelvisOrientation = body_interface.GetRotation(ragdoll.GetBodyID(0));
	mForward = GetHeadingForward(ragdoll.mTargets[0].Orientation, mRestPelvis);

	GatherFeet(ragdoll, body_interface, delta_time, contacts_fresh);

	if (contacts_fresh) {
		mbBodyOnGround = IsAnyBodyOnGround(ragdoll);
	}

	const float32 height = std::max(mCenterOfMass.Dot(mUp) - mGroundHeight, scMinPendulumHeight);
	mOmega = std::sqrt(mGravity / height);
	mCapturePoint = ToGround(mCenterOfMass + Horizontal(mVelocity) / mOmega);

	const JPH::Quat chest = body_interface.GetRotation(ragdoll.GetBodyID(mChestBody));
	const JPH::Vec3 chest_up = (chest * ragdoll.mBodies[mChestBody].RestOrientation.Conjugated()) * mUp;

	mTrunkTilt = std::acos(std::clamp(chest_up.Dot(mUp), -1.0f, 1.0f));
}

void RagdollBalance::GatherFeet(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time,
								bool contacts_fresh)
{
	float32 contact_ground = std::numeric_limits<float32>::max();

	for (RagdollLeg& leg : mLegs) {
		const JPH::BodyID foot_id = ragdoll.GetBodyID(leg.Foot);

		JPH::RVec3 position;
		JPH::Quat orientation;
		body_interface.GetPositionAndRotation(foot_id, position, orientation);

		leg.AnklePosition = JPH::Vec3(position);
		leg.FootOrientation = orientation;
		leg.FootCenter = leg.AnklePosition + orientation * leg.SoleCenter;

		leg.ContactAge += delta_time;

		if (contacts_fresh) {
			BodyGroundContact contact;
			gPhysics->pBackend->GetBodyGroundContact(foot_id, contact);

			leg.bTouching = (contact.Count > 0) && (Vec3f::ToJolt(contact.Normal).Dot(mUp) >= scSupportNormalMinUp);

			if (leg.bTouching) {
				leg.ContactAge = 0.0f;
			}
		}

		leg.SoleHeight = std::numeric_limits<float32>::max();

		for (uint32 corner = 0; corner < scSoleCorners; corner++) {
			leg.ContactCorners[corner] = leg.AnklePosition + orientation * leg.SoleCorners[corner];
			leg.SoleHeight = std::min(leg.SoleHeight, leg.ContactCorners[corner].Dot(mUp));
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
		mGroundHeight = std::min(mLegs[0].FootCenter.Dot(mUp), mLegs[1].FootCenter.Dot(mUp));
	}

	for (RagdollLeg& leg : mLegs) {
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

		BodyGroundContact contact;
		gPhysics->pBackend->GetBodyGroundContact(ragdoll.GetBodyID(i), contact);

		if (contact.Count > 0 && Vec3f::ToJolt(contact.Normal).Dot(mUp) >= scSupportNormalMinUp) {
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

	for (const RagdollLeg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		foot_center_sum += leg.FootCenter;
		stance_count++;

		for (uint32 corner = 0; corner < leg.ContactCornerCount; corner++) {
			points[count++] = ToPlane(leg.ContactCorners[corner]);
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

	mSupportCenter = FromPlane(center);

	ClosestOnHull(hull, mSupportCount, ToPlane(mCapturePoint), mCaptureOutside);

	Vec2f shrunk[scMaxPoints + 1];

	for (uint32 i = 0; i < mSupportCount; i++) {
		shrunk[i] = Vec2f { center.X + (hull[i].X - center.X) * scSupportShrink,
							center.Y + (hull[i].Y - center.Y) * scSupportShrink };
	}

	float32 unused_distance = 0.0f;
	const Vec2f held = ClosestOnHull(shrunk, mSupportCount, ToPlane(mCenterOfMass), unused_distance);
	const Vec2f target { held.X + (center.X - held.X) * scCentering, held.Y + (center.Y - held.Y) * scCentering };

	mSupportTarget = FromPlane(target);
}

bool RagdollBalance::DetectFall()
{
	eFallReason reason = eFallReason::None;

	if (mTrunkTilt > scFallTilt) {
		reason = eFallReason::Tilt;
	}
	else if (mCenterOfMass.Dot(mUp) - mGroundHeight < mRestComHeight * scFallHeightFraction) {
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

	if (mCaptureOutside < 0.0f && Horizontal(mVelocity).Length() < scCalmSpeed) {
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
	RagdollLeg& leg = mLegs[mSwing.Leg];

	mSwing.Time += delta_time;

	const float32 phase = mSwing.Time / scSwingTime;

	if (!mSwing.bFixed && phase < scSwingFreezeFraction) {
		mSwing.Landing = PlanLanding(mSwing.Leg, 1 - mSwing.Leg, scSwingTime - mSwing.Time);
	}

	const JPH::Quat landing_foot = GetHeadingRotation(mForward) * leg.RestFoot;
	const JPH::Vec3 landing_ankle = mSwing.Landing - landing_foot * leg.SoleCenter;

	const float32 travel = SmoothStep(phase / scSwingTravelFraction);
	const JPH::Vec3 start = Horizontal(mSwing.StartAnkle);
	const JPH::Vec3 end = Horizontal(landing_ankle);

	float32 height = mSwing.StartAnkle.Dot(mUp) + (landing_ankle.Dot(mUp) - mSwing.StartAnkle.Dot(mUp)) * travel;

	if (phase < 1.0f) {
		height += scSwingLift * std::sin(FX_PI * phase);
	}
	else {
		height -= scSwingPress;
	}

	mSwing.AnkleTarget = start + (end - start) * travel + mUp * height;

	if (!leg.bTouching) {
		mSwing.bLifted = true;
	}

	const bool near_landing = Horizontal(leg.AnklePosition - landing_ankle).Length() < scTouchdownDistance;
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
	if (mCaptureOutside < -scSettleInside && Horizontal(mVelocity).Length() < scSettleSpeed) {
		mSettleTime += delta_time;
	}
	else {
		mSettleTime = 0.0f;
	}

	if (mShiftLeg < 0) {
		if (mSettleTime > scSettleTime && NeedsClosingStep()) {
			const float32 distance_a = Horizontal(mCenterOfMass - mLegs[0].FootCenter).Length();
			const float32 distance_b = Horizontal(mCenterOfMass - mLegs[1].FootCenter).Length();

			mShiftLeg = (distance_a > distance_b) ? 0 : 1;
			mShiftTime = 0.0f;
		}

		return;
	}

	mShiftTime += delta_time;

	const uint32 swing_leg = static_cast<uint32>(mShiftLeg);
	const RagdollLeg& stance = mLegs[1 - swing_leg];

	if (GetCaptureDistance(stance) < -scShiftInside && Horizontal(mVelocity).Length() < scShiftSpeed) {
		const JPH::Vec3 side = mUp.Cross(mForward) * mLegs[swing_leg].SideSign;
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

	const JPH::Vec3 side = mUp.Cross(mForward);
	const JPH::Vec3 spread = Horizontal(mLegs[0].FootCenter - mLegs[1].FootCenter);

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
		float32 cost = Horizontal(landing - mLegs[swing].FootCenter).Length() +
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
	RagdollLeg& leg = mLegs[swing_leg];
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

float32 RagdollBalance::GetCaptureDistance(const RagdollLeg& leg) const
{
	Vec2f points[scSoleCorners];
	Vec2f hull[scSoleCorners + 1];

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		points[corner] = ToPlane(leg.ContactCorners[corner]);
	}

	const uint32 count = BuildHull(points, scSoleCorners, hull);

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, ToPlane(mCapturePoint), distance);

	return distance;
}

float32 RagdollBalance::GetLandingCaptureError(uint32 stance_leg, const JPH::Vec3 landing) const
{
	const RagdollLeg& stance = mLegs[stance_leg];
	const JPH::Vec3 side = mUp.Cross(mForward);

	Vec2f points[scSoleCorners * 2];
	Vec2f hull[scSoleCorners * 2 + 1];
	uint32 count = 0;

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		points[count++] = ToPlane(stance.ContactCorners[corner]);
	}

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		const JPH::Vec3 point = landing + side * (scSoleCornerSigns[corner][0] * mFootHalfWidth) +
								mForward * (scSoleCornerSigns[corner][1] * mFootHalfLength);
		points[count++] = ToPlane(point);
	}

	count = BuildHull(points, count, hull);

	const float32 growth = std::exp(mOmega * scSwingTime);
	const JPH::Vec3 pressure = stance.FootCenter;
	const JPH::Vec3 predicted = pressure + Horizontal(mCapturePoint - pressure) * growth;

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, ToPlane(predicted), distance);

	return std::max(distance + scCaptureMargin, 0.0f);
}

JPH::Vec3 RagdollBalance::PlanLanding(uint32 swing_leg, uint32 stance_leg, float32 time_left) const
{
	const RagdollLeg& stance = mLegs[stance_leg];
	const RagdollLeg& swing = mLegs[swing_leg];

	Vec2f stance_points[scSoleCorners];
	Vec2f stance_hull[scSoleCorners + 1];
	uint32 stance_count = 0;

	for (uint32 corner = 0; corner < stance.ContactCornerCount; corner++) {
		stance_points[stance_count++] = ToPlane(stance.ContactCorners[corner]);
	}

	if (stance_count == 0) {
		stance_points[stance_count++] = ToPlane(stance.FootCenter);
	}

	stance_count = BuildHull(stance_points, stance_count, stance_hull);

	const Vec2f capture = ToPlane(mCapturePoint);

	float32 unused_distance = 0.0f;
	const Vec2f pressure = ClosestOnHull(stance_hull, stance_count, capture, unused_distance);

	const float32 growth = std::exp(mOmega * std::max(time_left, 0.0f));
	const Vec2f predicted { pressure.X + (capture.X - pressure.X) * growth,
							pressure.Y + (capture.Y - pressure.Y) * growth };

	JPH::Vec3 direction = mAxisU * (predicted.X - pressure.X) + mAxisV * (predicted.Y - pressure.Y);
	direction = direction.NormalizedOr(Horizontal(mVelocity).NormalizedOr(mForward));

	const JPH::Vec3 side = mUp.Cross(mForward) * swing.SideSign;
	JPH::Vec3 landing = mAxisU * predicted.X + mAxisV * predicted.Y + direction * scLandingMargin +
						side * scLandingSideMargin;

	const JPH::Vec3 stance_center = Horizontal(stance.FootCenter);
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
	return Horizontal((orientation * rest.Conjugated()) * mRestForward).NormalizedOr(mRestForward);
}

JPH::Quat RagdollBalance::GetHeadingRotation(const JPH::Vec3 forward) const
{
	const JPH::Vec3 heading = Horizontal(forward).NormalizedOr(mRestForward);
	const float32 angle = std::atan2(mRestForward.Cross(heading).Dot(mUp), mRestForward.Dot(heading));

	return JPH::Quat::sRotation(mUp, angle);
}

JPH::Vec3 RagdollBalance::ToGround(const JPH::Vec3 point) const { return Horizontal(point) + mUp * mGroundHeight; }

} // namespace fx::physics
