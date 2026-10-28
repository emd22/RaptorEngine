#include "RagdollBalance.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"
#include "Ragdoll.hpp"

#include <Color.hpp>
#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Math/TwoBoneIK.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace fx::physics {

namespace {

constexpr float32 scSupportNormalMinUp = 0.7f;
constexpr float32 scContactHoldTime = 0.08f;
constexpr float32 scNearGround = 0.02f;
constexpr float32 scStancePress = 0.01f;
constexpr float32 scLiftStart = 0.003f;
constexpr float32 scLiftRange = 0.015f;
constexpr float32 scMinPendulumHeight = 0.3f;

constexpr float32 scStanceHeightFraction = 0.95f;
constexpr float32 scMinHipHeight = 0.25f;
constexpr float32 scStanceFrequencyScale = 0.15f;
constexpr float32 scMaxComShift = 0.12f;
constexpr float32 scComShiftGain = 3.0f;
constexpr float32 scSupportShrink = 0.5f;
constexpr float32 scCentering = 0.2f;
constexpr float32 scGravityFeedForward = 1.0f;

constexpr float32 scTorsoFrequency = 4.0f;
constexpr float32 scTorsoMaxTorque = 300.0f;
constexpr float32 scGravityFullStrength = 0.25f;

constexpr float32 scStepTrigger = 0.02f;
constexpr float32 scMinDoubleSupport = 0.05f;
constexpr float32 scSwingTime = 0.3f;
constexpr float32 scSwingFreezeFraction = 0.75f;
constexpr float32 scSwingTravelFraction = 0.85f;
constexpr float32 scSwingLift = 0.07f;
constexpr float32 scSwingPress = 0.03f;
constexpr float32 scSwingOvertime = 0.15f;
constexpr float32 scSwingMinTouchdown = 0.6f;
constexpr float32 scTouchdownDistance = 0.05f;
constexpr float32 scLandingMargin = 0.05f;
constexpr float32 scLandingSideMargin = 0.03f;
constexpr float32 scMaxStepWidth = 0.5f;
constexpr float32 scStanceDistanceCost = 2.0f;
constexpr float32 scCaptureErrorCost = 10.0f;
constexpr float32 scCaptureMargin = 0.05f;
constexpr float32 scMinStepWidth = 0.12f;
constexpr float32 scMaxStepLength = 0.75f;
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

constexpr float32 scMarkerSize = 0.06f;

float32 Cross2(const Vec2f origin, const Vec2f a, const Vec2f b) { return a.CrossOffset(b, origin); }

float32 Distance2(const Vec2f a, const Vec2f b) { return std::hypot(a.X - b.X, a.Y - b.Y); }

/**
 * @brief Convex hull of vertices via monotone chain. Writes the hull to `out_hull` counter-clockwise  with no repeated
 * closing vertex.
 * @returns The vertex count
 */
uint32 BuildHull(Vec2f* points, uint32 count, Vec2f* out_hull)
{
	if (count < 3) {
		std::copy(points, points + count, out_hull);
		return count;
	}

	std::sort(points, points + count,
			  [](const Vec2f& a, const Vec2f& b) { return (a.X < b.X) || (a.X == b.X && a.Y < b.Y); });

	uint32 size = 0;

	// Pops hull vertices until appending `point` makes a strict counter-clockwise turn, never shrinking
	// the hull below `min_size`, then appends it.
	auto push = [&](const Vec2f& point, uint32 min_size)
	{
		while (size >= min_size && Cross2(out_hull[size - 2], out_hull[size - 1], point) <= 0.0f) {
			size--;
		}
		out_hull[size++] = point;
	};

	// Lower hull, left to right.
	for (uint32 i = 0; i < count; i++) {
		push(points[i], 2);
	}

	// Upper hull, right to left. The rightmost point already ends the lower hull, so start one before it,
	// and keep min_size above the lower hull so this pass can't pop into it.
	const uint32 upper_min_size = size + 1;

	for (uint32 i = count - 1; i > 0; i--) {
		push(points[i - 1], upper_min_size);
	}

	// The upper pass finishes by appending points[0] again, which duplicates the first vertex.
	return size - 1;
}

Vec2f ClosestOnSegment(const Vec2f a, const Vec2f b, const Vec2f p)
{
	const float32 dx = b.X - a.X;
	const float32 dy = b.Y - a.Y;
	const float32 length_sq = dx * dx + dy * dy;

	if (length_sq < 1e-10f) {
		return a;
	}

	const float32 t = std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / length_sq, 0.0f, 1.0f);

	return Vec2f { a.X + dx * t, a.Y + dy * t };
}

Vec2f ClosestOnHull(const Vec2f* hull, uint32 count, const Vec2f p, float32& out_signed_distance)
{
	if (count == 0) {
		out_signed_distance = 0.0f;
		return p;
	}

	if (count == 1) {
		out_signed_distance = Distance2(p, hull[0]);
		return hull[0];
	}

	bool inside = (count >= 3);
	Vec2f closest = hull[0];
	float32 closest_distance = std::numeric_limits<float32>::max();
	const uint32 edge_count = (count == 2) ? 1 : count;

	for (uint32 i = 0; i < edge_count; i++) {
		const Vec2f a = hull[i];
		const Vec2f b = hull[(i + 1) % count];

		if (Cross2(a, b, p) < 0.0f) {
			inside = false;
		}

		const Vec2f on_edge = ClosestOnSegment(a, b, p);
		const float32 distance = Distance2(p, on_edge);

		if (distance < closest_distance) {
			closest_distance = distance;
			closest = on_edge;
		}
	}

	if (inside) {
		out_signed_distance = -closest_distance;
		return p;
	}

	out_signed_distance = closest_distance;
	return closest;
}

Vec2f GetHullCentroid(const Vec2f* hull, uint32 count)
{
	Vec2f average;

	for (uint32 i = 0; i < count; i++) {
		average.X += hull[i].X / static_cast<float32>(count);
		average.Y += hull[i].Y / static_cast<float32>(count);
	}

	float32 area = 0.0f;
	Vec2f weighted;

	for (uint32 i = 0; i < count; i++) {
		const Vec2f a { hull[i].X - average.X, hull[i].Y - average.Y };
		const Vec2f b { hull[(i + 1) % count].X - average.X, hull[(i + 1) % count].Y - average.Y };
		const float32 cross = a.X * b.Y - a.Y * b.X;

		area += cross;
		weighted.X += (a.X + b.X) * cross;
		weighted.Y += (a.Y + b.Y) * cross;
	}

	if (std::abs(area) < 1e-8f) {
		return average;
	}

	return Vec2f { average.X + weighted.X / (3.0f * area), average.Y + weighted.Y / (3.0f * area) };
}

JPH::Vec3 GetRotationVector(const JPH::Quat& rotation)
{
	JPH::Quat shortest = rotation;

	if (shortest.GetW() < 0.0f) {
		shortest = -shortest;
	}

	JPH::Vec3 axis;
	float32 angle = 0.0f;
	shortest.GetAxisAngle(axis, angle);

	return axis * angle;
}

JPH::Quat FrameRotation(const JPH::Vec3 local_axis, const JPH::Vec3 local_hinge, const JPH::Vec3 world_axis,
						const JPH::Vec3 world_hinge)
{
	const JPH::Vec3 axis_l = local_axis.Normalized();
	const JPH::Vec3 hinge_l = (local_hinge - axis_l * local_hinge.Dot(axis_l))
								  .NormalizedOr(axis_l.GetNormalizedPerpendicular());
	const JPH::Vec3 axis_w = world_axis.Normalized();
	const JPH::Vec3 hinge_w = (world_hinge - axis_w * world_hinge.Dot(axis_w))
								  .NormalizedOr(axis_w.GetNormalizedPerpendicular());

	const JPH::Mat44 local(JPH::Vec4(axis_l, 0.0f), JPH::Vec4(hinge_l, 0.0f), JPH::Vec4(axis_l.Cross(hinge_l), 0.0f),
						   JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
	const JPH::Mat44 world(JPH::Vec4(axis_w, 0.0f), JPH::Vec4(hinge_w, 0.0f), JPH::Vec4(axis_w.Cross(hinge_w), 0.0f),
						   JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));

	return (world * local.Transposed3x3()).GetQuaternion().Normalized();
}

Vec3f ToVec3f(const JPH::Vec3 v) { return Vec3f(v.GetX(), v.GetY(), v.GetZ()); }

JPH::Vec3 ToJolt(const Vec3f v) { return JPH::Vec3(v.X, v.Y, v.Z); }

float32 SmoothStep(float32 t)
{
	const float32 x = std::clamp(t, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

} // namespace

bool RagdollBalance::Init(Ragdoll& ragdoll)
{
	mbValid = false;

	const SizedArray<Ragdoll::BodyInfo>& bodies = ragdoll.mBodies;

	if (bodies.Size < 2) {
		return false;
	}

	const JPH::Vec3 gravity = gPhysics->pBackend->PhysicsSystem.GetGravity();
	mUp = (-gravity).NormalizedOr(JPH::Vec3::sAxisY());
	mGravity = gravity.Length();

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

		if (thigh <= 0 || bodies[thigh].ParentBody != 0 || !bodies[shin].bHinge || leg_count >= scLegCount) {
			continue;
		}

		Leg& leg = mLegs[leg_count++];
		leg = Leg {};
		leg.Thigh = static_cast<uint32>(thigh);
		leg.Shin = static_cast<uint32>(shin);
		leg.Foot = i;
		leg.Upper = (bodies[shin].RestPosition - bodies[thigh].RestPosition).Length();
		leg.Lower = (foot.RestPosition - bodies[shin].RestPosition).Length();
		leg.HipOffset = bodies[thigh].ParentAnchor;
		leg.RestFoot = foot.RestOrientation;

		const JPH::Vec3 half = foot.BoxHalfExtent;
		const float32 signs[scSoleCorners][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };

		for (uint32 corner = 0; corner < scSoleCorners; corner++) {
			leg.SoleCorners[corner] = foot.LocalCenter + foot.BoxRotation * JPH::Vec3(signs[corner][0] * half.GetX(),
																					  -half.GetY(),
																					  signs[corner][1] * half.GetZ());
		}

		leg.SoleCenter = foot.LocalCenter + foot.BoxRotation * JPH::Vec3(0.0f, -half.GetY(), 0.0f);

		mFootHalfWidth = half.GetX();
		mFootHalfLength = half.GetZ();

		forward += foot.RestOrientation * foot.BoxRotation * JPH::Vec3::sAxisZ();
	}

	if (leg_count != scLegCount || mNeckBody == 0 || bodies[mNeckBody].ParentBody < 0) {
		return false;
	}

	mChestBody = static_cast<uint32>(bodies[mNeckBody].ParentBody);

	const Ragdoll::BodyInfo& pelvis = bodies[0];

	mRestPelvis = pelvis.RestOrientation;
	mRestForward = Horizontal(forward).NormalizedOr(JPH::Vec3::sAxisZ());
	mForward = mRestForward;

	const JPH::Vec3 side = mUp.Cross(mRestForward);
	float32 rest_ground = std::numeric_limits<float32>::max();

	for (Leg& leg : mLegs) {
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

	mbValid = true;
	Reset();

	return true;
}

void RagdollBalance::Reset()
{
	for (Leg& leg : mLegs) {
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

bool RagdollBalance::IsLoaded(const Leg& leg) const { return leg.bStance && leg.ContactAge < scContactHoldTime; }

JPH::Vec3 RagdollBalance::ToGround(const JPH::Vec3 point) const { return Horizontal(point) + mUp * mGroundHeight; }

void RagdollBalance::GatherState(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time,
								 const JPH::Vec3 gravity, bool contacts_fresh)
{
	mGravity = gravity.Length();
	mUp = (-gravity).NormalizedOr(JPH::Vec3::sAxisY());
	mAxisU = mUp.GetNormalizedPerpendicular();
	mAxisV = mUp.Cross(mAxisU);

	float32 total_mass = 0.0f;
	JPH::Vec3 weighted_center = JPH::Vec3::sZero();
	JPH::Vec3 momentum = JPH::Vec3::sZero();

	for (uint32 i = 0; i < ragdoll.mBodies.Size; i++) {
		const float32 mass = ragdoll.mBodies[i].Mass;
		const JPH::BodyID body_id = ragdoll.GetBodyID(i);

		weighted_center += ragdoll.mCenters[i] * mass;
		momentum += body_interface.GetLinearVelocity(body_id) * mass;
		total_mass += mass;
	}

	mCenterOfMass = weighted_center / total_mass;
	mVelocity = momentum / total_mass;

	const JPH::BodyID pelvis_id = ragdoll.GetBodyID(0);
	mPelvisOrientation = body_interface.GetRotation(pelvis_id);
	mPelvisAngularVelocity = body_interface.GetAngularVelocity(pelvis_id);
	mForward = GetHeadingForward(ragdoll.mTargets[0].Orientation, mRestPelvis);

	float32 contact_ground = std::numeric_limits<float32>::max();

	for (Leg& leg : mLegs) {
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

			const JPH::Vec3 normal(contact.Normal.X, contact.Normal.Y, contact.Normal.Z);
			leg.bTouching = (contact.Count > 0) && (normal.Dot(mUp) >= scSupportNormalMinUp);

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

	for (Leg& leg : mLegs) {
		leg.bContact = (leg.ContactAge < scContactHoldTime) ||
					   (mbHasGround && leg.SoleHeight - mGroundHeight < scNearGround);
		leg.ContactCornerCount = 0;

		if (leg.bContact) {
			leg.ContactCornerCount = scSoleCorners;
		}
	}

	if (contacts_fresh) {
		mbBodyOnGround = false;

		for (uint32 i = 0; i < ragdoll.mBodies.Size; i++) {
			const Ragdoll::BodyInfo& body = ragdoll.mBodies[i];

			if (body.Part == eRagdollBodyPart::Leg && i != mLegs[0].Thigh && i != mLegs[1].Thigh) {
				continue;
			}

			BodyGroundContact contact;
			gPhysics->pBackend->GetBodyGroundContact(ragdoll.GetBodyID(i), contact);

			const JPH::Vec3 normal(contact.Normal.X, contact.Normal.Y, contact.Normal.Z);

			if (contact.Count > 0 && normal.Dot(mUp) >= scSupportNormalMinUp) {
				mbBodyOnGround = true;
			}
		}
	}

	const float32 height = std::max(mCenterOfMass.Dot(mUp) - mGroundHeight, scMinPendulumHeight);
	mOmega = std::sqrt(mGravity / height);
	mCapturePoint = ToGround(mCenterOfMass + Horizontal(mVelocity) / mOmega);

	const JPH::Quat chest = body_interface.GetRotation(ragdoll.GetBodyID(mChestBody));
	const JPH::Vec3 chest_up = (chest * ragdoll.mBodies[mChestBody].RestOrientation.Conjugated()) * mUp;

	mTrunkTilt = std::acos(std::clamp(chest_up.Dot(mUp), -1.0f, 1.0f));
}

void RagdollBalance::BuildSupport()
{
	constexpr uint32 scMaxPoints = scLegCount * scSoleCorners;

	Vec2f points[scMaxPoints];
	uint32 count = 0;

	JPH::Vec3 foot_center_sum = JPH::Vec3::sZero();
	uint32 stance_count = 0;

	for (const Leg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		foot_center_sum += leg.FootCenter;
		stance_count++;

		for (uint32 corner = 0; corner < leg.ContactCornerCount; corner++) {
			points[count++] = Vec2f { leg.ContactCorners[corner].Dot(mAxisU), leg.ContactCorners[corner].Dot(mAxisV) };
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

	for (uint32 i = 0; i < mSupportCount; i++) {
		mSupport[i] = mAxisU * hull[i].X + mAxisV * hull[i].Y + mUp * mGroundHeight;
	}

	const Vec2f center = GetHullCentroid(hull, mSupportCount);

	mSupportCenter = mAxisU * center.X + mAxisV * center.Y + mUp * mGroundHeight;

	const Vec2f capture { mCapturePoint.Dot(mAxisU), mCapturePoint.Dot(mAxisV) };
	ClosestOnHull(hull, mSupportCount, capture, mCaptureOutside);

	Vec2f shrunk[scMaxPoints + 1];

	for (uint32 i = 0; i < mSupportCount; i++) {
		shrunk[i] = Vec2f { center.X + (hull[i].X - center.X) * scSupportShrink,
							center.Y + (hull[i].Y - center.Y) * scSupportShrink };
	}

	const Vec2f com { mCenterOfMass.Dot(mAxisU), mCenterOfMass.Dot(mAxisV) };
	float32 unused_distance = 0.0f;
	const Vec2f held = ClosestOnHull(shrunk, mSupportCount, com, unused_distance);
	const Vec2f target { held.X + (center.X - held.X) * scCentering, held.Y + (center.Y - held.Y) * scCentering };

	mSupportTarget = mAxisU * target.X + mAxisV * target.Y + mUp * mGroundHeight;
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

float32 RagdollBalance::GetCaptureDistance(const Leg& leg) const
{
	Vec2f points[scSoleCorners];
	Vec2f hull[scSoleCorners + 1];

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		points[corner] = Vec2f { leg.ContactCorners[corner].Dot(mAxisU), leg.ContactCorners[corner].Dot(mAxisV) };
	}

	const uint32 count = BuildHull(points, scSoleCorners, hull);
	const Vec2f capture { mCapturePoint.Dot(mAxisU), mCapturePoint.Dot(mAxisV) };

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, capture, distance);

	return distance;
}

float32 RagdollBalance::GetLandingCaptureError(uint32 stance_leg, const JPH::Vec3 landing) const
{
	const Leg& stance = mLegs[stance_leg];
	const JPH::Vec3 side = mUp.Cross(mForward);

	Vec2f points[scSoleCorners * 2];
	Vec2f hull[scSoleCorners * 2 + 1];
	uint32 count = 0;

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		points[count++] = Vec2f { stance.ContactCorners[corner].Dot(mAxisU),
								  stance.ContactCorners[corner].Dot(mAxisV) };
	}

	const float32 signs[scSoleCorners][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };

	for (uint32 corner = 0; corner < scSoleCorners; corner++) {
		const JPH::Vec3 point = landing + side * (signs[corner][0] * mFootHalfWidth) +
								mForward * (signs[corner][1] * mFootHalfLength);
		points[count++] = Vec2f { point.Dot(mAxisU), point.Dot(mAxisV) };
	}

	count = BuildHull(points, count, hull);

	const float32 growth = std::exp(mOmega * scSwingTime);
	const JPH::Vec3 pressure = stance.FootCenter;
	const JPH::Vec3 predicted = pressure + Horizontal(mCapturePoint - pressure) * growth;
	const Vec2f capture { predicted.Dot(mAxisU), predicted.Dot(mAxisV) };

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, capture, distance);

	return std::max(distance + scCaptureMargin, 0.0f);
}

JPH::Vec3 RagdollBalance::PlanLanding(uint32 swing_leg, uint32 stance_leg, float32 time_left) const
{
	const Leg& stance = mLegs[stance_leg];
	const Leg& swing = mLegs[swing_leg];

	Vec2f stance_points[scSoleCorners];
	Vec2f stance_hull[scSoleCorners + 1];
	uint32 stance_count = 0;

	for (uint32 corner = 0; corner < stance.ContactCornerCount; corner++) {
		stance_points[stance_count++] = Vec2f { stance.ContactCorners[corner].Dot(mAxisU),
												stance.ContactCorners[corner].Dot(mAxisV) };
	}

	if (stance_count == 0) {
		stance_points[stance_count++] = Vec2f { stance.FootCenter.Dot(mAxisU), stance.FootCenter.Dot(mAxisV) };
	}

	stance_count = BuildHull(stance_points, stance_count, stance_hull);

	const Vec2f capture { mCapturePoint.Dot(mAxisU), mCapturePoint.Dot(mAxisV) };
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

bool RagdollBalance::BeginStep()
{
	if (mStatus.RecoverySteps >= scMaxRecoverySteps) {
		Fall(eFallReason::StepBudget);
		return false;
	}

	int32 best_leg = -1;
	float32 best_cost = std::numeric_limits<float32>::max();
	JPH::Vec3 best_landing = JPH::Vec3::sZero();

	for (uint32 swing = 0; swing < scLegCount; swing++) {
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
	Leg& leg = mLegs[swing_leg];
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
	const Leg& stance = mLegs[1 - swing_leg];

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
	Leg& leg = mLegs[mSwing.Leg];

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

	if (touched_down || mSwing.Time > scSwingTime + scSwingOvertime) {
		leg.bStance = true;

		mStatus.State = eBalanceState::Standing;
		mStatus.Steps++;
		mDoubleSupportTime = 0.0f;

		if (!mSwing.bFixed) {
			mStatus.RecoverySteps++;
		}
	}
}

void RagdollBalance::SolveLeg(Ragdoll& ragdoll, const Leg& leg, const JPH::Vec3 hip, const JPH::Vec3 ankle,
							  JPH::Quat& out_thigh, JPH::Quat& out_shin) const
{
	const TwoBoneIKResult ik = SolveTwoBoneIK(ToVec3f(hip), ToVec3f(ankle), leg.Upper, leg.Lower, ToVec3f(mForward));

	const JPH::Vec3 knee = ToJolt(ik.Mid);
	const JPH::Vec3 end = ToJolt(ik.End);

	const JPH::Vec3 thigh_direction = (knee - hip).NormalizedOr(-mUp);
	const JPH::Vec3 shin_direction = (end - knee).NormalizedOr(-mUp);
	const JPH::Vec3 leg_direction = (end - hip).NormalizedOr(-mUp);

	const JPH::Vec3 hinge = leg_direction.Cross(mForward).NormalizedOr(mUp.Cross(mForward)) * leg.HingeSign;

	const Ragdoll::BodyInfo& thigh = ragdoll.mBodies[leg.Thigh];
	const Ragdoll::BodyInfo& shin = ragdoll.mBodies[leg.Shin];

	out_thigh = FrameRotation(thigh.LocalAxis, shin.ParentHingeLocal, thigh_direction, hinge);
	out_shin = FrameRotation(shin.LocalAxis, shin.HingeLocal, shin_direction, hinge);
}

void RagdollBalance::DriveStanceLegs(Ragdoll& ragdoll)
{
	const JPH::Quat pelvis_target = ragdoll.mTargets[0].Orientation;

	JPH::Vec3 center = mSupportTarget;

	if (mShiftLeg >= 0) {
		center = mLegs[1 - mShiftLeg].FootCenter;
	}

	JPH::Vec3 shift = Horizontal(center - mCapturePoint) * scComShiftGain;
	const float32 shift_length = shift.Length();

	if (shift_length > scMaxComShift) {
		shift *= scMaxComShift / shift_length;
	}

	const JPH::Vec3 pelvis_horizontal = Horizontal(ragdoll.mOrigins[0]) + shift;

	float32 pelvis_height = std::numeric_limits<float32>::max();

	for (const Leg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		const JPH::Vec3 hip_offset = pelvis_target * leg.HipOffset;
		const float32 reach = scStanceHeightFraction * (leg.Upper + leg.Lower);
		const float32 spread = Horizontal(pelvis_horizontal + hip_offset - leg.AnklePosition).Length();
		const float32 hip_height = std::sqrt(std::max(reach * reach - spread * spread, reach * reach * scMinHipHeight));

		pelvis_height = std::min(pelvis_height, leg.AnklePosition.Dot(mUp) + hip_height - hip_offset.Dot(mUp));
	}

	if (pelvis_height == std::numeric_limits<float32>::max()) {
		return;
	}

	const JPH::Vec3 pelvis = pelvis_horizontal + mUp * pelvis_height;


	for (Leg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		const JPH::Vec3 hip = pelvis + pelvis_target * leg.HipOffset;

		const JPH::Vec3 foot_forward = GetHeadingForward(leg.FootOrientation, leg.RestFoot);
		const JPH::Quat foot = GetHeadingRotation(foot_forward) * leg.RestFoot;

		if (!IsLoaded(leg)) {
			const JPH::Vec3 ankle = leg.AnklePosition -
									mUp * (std::max(leg.SoleHeight - mGroundHeight, 0.0f) + scSwingPress);
			DriveFreeLeg(ragdoll, leg, ankle, foot);
			continue;
		}

		const JPH::Vec3 ankle = leg.AnklePosition -
								mUp * (std::max(leg.SoleHeight - mGroundHeight, 0.0f) + scStancePress);

		JPH::Quat thigh;
		JPH::Quat shin;
		SolveLeg(ragdoll, leg, hip, ankle, thigh, shin);

		leg.DesiredThigh = thigh;


		Ragdoll::MotorOverride& knee_motor = ragdoll.mMotorOverrides[leg.Shin];
		knee_motor.Mode = Ragdoll::eMotorOverride::Target;
		knee_motor.Target = (thigh.Conjugated() * shin).Normalized();
		knee_motor.FrequencyScale = scStanceFrequencyScale;
		knee_motor.bSupported = true;

		Ragdoll::MotorOverride& ankle_motor = ragdoll.mMotorOverrides[leg.Foot];
		ankle_motor.Mode = Ragdoll::eMotorOverride::Target;
		ankle_motor.Target = (shin.Conjugated() * foot).Normalized();
		ankle_motor.FrequencyScale = scStanceFrequencyScale;
		ankle_motor.bSupported = true;

		ragdoll.mNoGravityComp[leg.Thigh] = 1;
		ragdoll.mNoGravityComp[leg.Shin] = 1;
		ragdoll.mNoGravityComp[leg.Foot] = 1;
	}
}

void RagdollBalance::DriveSwingLeg(Ragdoll& ragdoll)
{
	const Leg& leg = mLegs[mSwing.Leg];

	DriveFreeLeg(ragdoll, leg, mSwing.AnkleTarget, GetHeadingRotation(mForward) * leg.RestFoot);
}

void RagdollBalance::DriveFreeLeg(Ragdoll& ragdoll, const Leg& leg, const JPH::Vec3 ankle, const JPH::Quat& foot)
{
	JPH::Quat thigh;
	JPH::Quat shin;
	SolveLeg(ragdoll, leg, ragdoll.mOrigins[leg.Thigh], ankle, thigh, shin);

	Ragdoll::MotorOverride& hip_motor = ragdoll.mMotorOverrides[leg.Thigh];
	hip_motor.Mode = Ragdoll::eMotorOverride::Target;
	hip_motor.Target = (mPelvisOrientation.Conjugated() * thigh).Normalized();

	Ragdoll::MotorOverride& knee_motor = ragdoll.mMotorOverrides[leg.Shin];
	knee_motor.Mode = Ragdoll::eMotorOverride::Target;
	knee_motor.Target = (thigh.Conjugated() * shin).Normalized();

	Ragdoll::MotorOverride& ankle_motor = ragdoll.mMotorOverrides[leg.Foot];
	ankle_motor.Mode = Ragdoll::eMotorOverride::Target;
	ankle_motor.Target = (shin.Conjugated() * foot).Normalized();
}

void RagdollBalance::ApplyTorsoControl(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, const JPH::Vec3 gravity)
{
	uint32 stance_legs[scLegCount];
	uint32 stance_count = 0;

	for (uint32 i = 0; i < scLegCount; i++) {
		if (IsLoaded(mLegs[i])) {
			stance_legs[stance_count++] = i;
		}
	}

	if (stance_count == 0) {
		return;
	}

	JPH::Vec3 pivot = JPH::Vec3::sZero();
	float32 upper_mass = ragdoll.mSubtreeMass[0];
	JPH::Vec3 upper_moment = ragdoll.mSubtreeMoment[0];

	for (uint32 i = 0; i < stance_count; i++) {
		const uint32 thigh = mLegs[stance_legs[i]].Thigh;

		pivot += ragdoll.mOrigins[thigh];
		upper_mass -= ragdoll.mSubtreeMass[thigh];
		upper_moment -= ragdoll.mSubtreeMoment[thigh];
	}

	pivot /= static_cast<float32>(stance_count);

	if (upper_mass <= 0.0f) {
		return;
	}

	float32 inertia = ragdoll.GetRawSubtreeInertia(0, pivot);

	for (uint32 i = 0; i < stance_count; i++) {
		inertia -= ragdoll.GetRawSubtreeInertia(mLegs[stance_legs[i]].Thigh, pivot);
	}

	inertia = std::max(inertia, 1e-2f);

	float32 weights[scLegCount] = { 1.0f, 0.0f };

	if (stance_count == 2) {
		const float32 distance_a = Horizontal(mCenterOfMass - mLegs[stance_legs[0]].FootCenter).Length();
		const float32 distance_b = Horizontal(mCenterOfMass - mLegs[stance_legs[1]].FootCenter).Length();

		weights[0] = distance_b / std::max(distance_a + distance_b, 1e-4f);
		weights[1] = 1.0f - weights[0];
	}

	const JPH::Quat pelvis_target = ragdoll.mTargets[0].Orientation;
	const float32 frequency_scale = scTorsoFrequency / ragdoll.mBodies[mLegs[0].Thigh].Muscle.Stiffness;

	for (uint32 i = 0; i < stance_count; i++) {
		const Leg& leg = mLegs[stance_legs[i]];
		const JPH::Quat thigh = body_interface.GetRotation(ragdoll.GetBodyID(leg.Thigh));
		const float32 lift = std::clamp((leg.SoleHeight - mGroundHeight - scLiftStart) / scLiftRange, 0.0f, 1.0f);
		const JPH::Quat hip_thigh = thigh.SLERP(leg.DesiredThigh, lift).Normalized();

		Ragdoll::MotorOverride& hip_motor = ragdoll.mMotorOverrides[leg.Thigh];
		hip_motor.Mode = Ragdoll::eMotorOverride::Target;
		hip_motor.Target = (pelvis_target.Conjugated() * hip_thigh).Normalized();
		hip_motor.FrequencyScale = frequency_scale;
		hip_motor.Inertia = std::max(inertia * weights[i], 1e-2f);
	}

	const JPH::Vec3 upper_center = upper_moment / upper_mass;
	JPH::Vec3 torque = -(upper_center - pivot).Cross(gravity * upper_mass) * scGravityFeedForward *
					   std::min(ragdoll.mTargets[0].Strength / scGravityFullStrength, 1.0f);

	const float32 torque_length = torque.Length();

	if (torque_length > scTorsoMaxTorque) {
		torque *= scTorsoMaxTorque / torque_length;
	}

	body_interface.AddTorque(ragdoll.GetBodyID(0), torque);

	for (uint32 i = 0; i < stance_count; i++) {
		body_interface.AddTorque(ragdoll.GetBodyID(mLegs[stance_legs[i]].Thigh), -torque * weights[i]);
	}
}

void RagdollBalance::Step(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time,
						  const JPH::Vec3 gravity, bool contacts_fresh)
{
	ragdoll.ClearMotorOverrides();

	if (!mbValid || mStatus.State == eBalanceState::Off || mStatus.State == eBalanceState::Fallen) {
		return;
	}

	for (const Ragdoll::BodyTarget& target : ragdoll.mTargets) {
		if (target.Mode != eRagdollBodyMode::Powered) {
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

	UpdateStatus();

	if (mStatus.State == eBalanceState::Fallen) {
		return;
	}

	DriveStanceLegs(ragdoll);

	if (mStatus.State == eBalanceState::Stepping) {
		DriveSwingLeg(ragdoll);
	}

	ApplyTorsoControl(ragdoll, body_interface, gravity);
}

void RagdollBalance::UpdateStatus()
{
	mStatus.CaptureOutside = mCaptureOutside;
	mStatus.CenterOfMass = mCenterOfMass;
	mStatus.Velocity = mVelocity;
	mStatus.CapturePoint = mCapturePoint;
	mStatus.TrunkTilt = mTrunkTilt;
	mStatus.Forward = mForward;
	mStatus.ComHeight = mCenterOfMass.Dot(mUp) - mGroundHeight;
	mStatus.SupportCenter = mSupportCenter;
	mStatus.bShifting = (mShiftLeg >= 0);
	mStatus.SoleHeights[0] = mLegs[0].SoleHeight - mGroundHeight;
	mStatus.SoleHeights[1] = mLegs[1].SoleHeight - mGroundHeight;
	mStatus.ContactAges[0] = mLegs[0].ContactAge;
	mStatus.ContactAges[1] = mLegs[1].ContactAge;
	mStatus.bClosingStep = (mStatus.State == eBalanceState::Stepping) && mSwing.bFixed;
	mStatus.FootCenters[0] = mLegs[0].FootCenter;
	mStatus.FootCenters[1] = mLegs[1].FootCenter;
	mStatus.FootContacts = static_cast<uint8>((mLegs[0].bContact ? 1 : 0) | (mLegs[1].bContact ? 2 : 0));
	mStatus.bCalm = (mStatus.State == eBalanceState::Standing) && (mCalmTime > scCalmTime) && (mShiftLeg < 0);
}

void RagdollBalance::DebugDraw() const
{
	if (!mbValid || mStatus.State == eBalanceState::Off) {
		return;
	}

	static const Color scCaptureColor = Color::FromRGBA(255, 230, 40, 255);
	static const Color scCaptureOutsideColor = Color::FromRGBA(255, 60, 40, 255);
	static const Color scCenterColor = Color::FromRGBA(90, 220, 255, 255);
	static const Color scSwingColor = Color::FromRGBA(255, 70, 220, 255);

	const JPH::Vec3 side = mUp.Cross(mForward);

	const auto draw_cross = [&](const JPH::Vec3 point, const Color color)
	{
		renderer::gDebugDraw->Line(ToVec3f(point - mForward * scMarkerSize), ToVec3f(point + mForward * scMarkerSize),
								   color);
		renderer::gDebugDraw->Line(ToVec3f(point - side * scMarkerSize), ToVec3f(point + side * scMarkerSize), color);
	};

	Color capture_color = scCaptureColor;

	if (mCaptureOutside > 0.0f) {
		capture_color = scCaptureOutsideColor;
	}

	draw_cross(mCapturePoint, capture_color);
	draw_cross(mSupportCenter, scCenterColor);
	renderer::gDebugDraw->Line(ToVec3f(ToGround(mCenterOfMass)), ToVec3f(mCapturePoint), capture_color);

	if (mStatus.State == eBalanceState::Stepping) {
		draw_cross(mSwing.Landing, scSwingColor);
		renderer::gDebugDraw->Line(ToVec3f(mLegs[mSwing.Leg].AnklePosition), ToVec3f(mSwing.AnkleTarget), scSwingColor);
	}
}

} // namespace fx::physics
