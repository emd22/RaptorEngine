#include "Ragdoll.hpp"
#include "RagdollBalance.hpp"

#include <Math/MathConsts.hpp>
#include <Math/TwoBoneIK.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scStancePress = 0.01f;
constexpr float32 scLiftStart = 0.003f;
constexpr float32 scLiftRange = 0.015f;

constexpr float32 scStanceHeightFraction = 0.9f;
constexpr float32 scMinHipHeight = 0.25f;
constexpr float32 scStanceFrequencyScale = 0.15f;
constexpr float32 scMaxComShift = 0.12f;
constexpr float32 scComShiftGain = 3.0f;

constexpr float32 scTorsoFrequency = 4.0f;
constexpr float32 scTorsoMaxTorque = 300.0f;
constexpr float32 scGravityFeedForward = 1.0f;
constexpr float32 scGravityFullStrength = 0.25f;

/// Centre of mass control frequency (Hz) and damping ratio; its force limits come from BalanceAssist
constexpr float32 scComFrequency = 2.0f;
constexpr float32 scComDamping = 1.0f;
/// Stagger speed (m/s) at which holding the centre of mass over the feet has fully given way to following the stagger
constexpr float32 scStaggerHoldSpeed = 0.25f;

JPH::Vec3 ClampLength(const JPH::Vec3 v, float32 max_length)
{
	const float32 length = v.Length();

	if (length <= max_length) {
		return v;
	}

	return v * (max_length / length);
}

} // namespace

bool RagdollBalance::IsLoaded(const BalanceLeg& leg) const { return leg.bStance && leg.ContactAge < scContactHoldTime; }

void RagdollBalance::DriveStanceLegs(Ragdoll& ragdoll)
{
	const JPH::Quat pelvis_target = mPelvisTarget;

	JPH::Vec3 center = mSupportTarget;

	if (mShiftLeg >= 0) {
		center = mLegs[1 - mShiftLeg].FootCenter;
	}

	JPH::Vec3 shift = mFrame.Horizontal(center - mCapturePoint) * scComShiftGain;
	const float32 shift_length = shift.Length();

	if (shift_length > scMaxComShift) {
		shift *= scMaxComShift / shift_length;
	}

	const JPH::Vec3 pelvis_horizontal = mFrame.Horizontal(ragdoll.mStates[0].Origin) + shift;

	float32 pelvis_height = std::numeric_limits<float32>::max();

	for (const BalanceLeg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		const JPH::Vec3 hip_offset = pelvis_target * leg.HipOffset;
		const float32 reach = scStanceHeightFraction * (leg.Upper + leg.Lower);
		const float32 spread = mFrame.Horizontal(pelvis_horizontal + hip_offset - leg.AnklePosition).Length();

		const float32 reach2 = reach * reach;
		const float32 spread2 = spread * spread;

		const float32 hip_height = std::sqrt(std::max(reach2 - spread2, reach2 * scMinHipHeight));

		pelvis_height = std::min(pelvis_height,
								 leg.AnklePosition.Dot(mFrame.Up) + hip_height - hip_offset.Dot(mFrame.Up));
	}

	if (pelvis_height == std::numeric_limits<float32>::max()) {
		return;
	}

	const JPH::Vec3 pelvis = pelvis_horizontal + mFrame.Up * (pelvis_height - mLandingDip);

	for (BalanceLeg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		const JPH::Vec3 hip = pelvis + pelvis_target * leg.HipOffset;

		const JPH::Vec3 foot_forward = GetHeadingForward(leg.FootOrientation, leg.RestFoot);
		const JPH::Quat foot = GetHeadingRotation(foot_forward) * leg.RestFoot;

		if (!IsLoaded(leg)) {
			const JPH::Vec3 ankle = leg.AnklePosition -
									mFrame.Up * (std::max(leg.SoleHeight - mGroundHeight, 0.0f) + scSwingPress);
			DriveFreeLeg(ragdoll, leg, ankle, foot);
			continue;
		}

		const JPH::Vec3 ankle = leg.AnklePosition -
								mFrame.Up * (std::max(leg.SoleHeight - mGroundHeight, 0.0f) + scStancePress);

		JPH::Quat thigh;
		JPH::Quat shin;
		SolveLeg(ragdoll, leg, hip, ankle, thigh, shin);

		leg.DesiredThigh = thigh;

		// Knee and ankle carry the body above them, so their stiffness comes from the supported inertia
		ragdoll.SetMotorTarget(leg.Shin, thigh, shin, scStanceFrequencyScale, true);
		ragdoll.SetMotorTarget(leg.Foot, shin, foot, scStanceFrequencyScale, true);

		ragdoll.mMotorOverrides[leg.Thigh].bNoGravityComp = true;
		ragdoll.mMotorOverrides[leg.Shin].bNoGravityComp = true;
		ragdoll.mMotorOverrides[leg.Foot].bNoGravityComp = true;
	}
}

void RagdollBalance::DriveSwingLeg(Ragdoll& ragdoll)
{
	const BalanceLeg& leg = mLegs[mSwing.Leg];

	DriveFreeLeg(ragdoll, leg, mSwing.AnkleTarget, GetHeadingRotation(mForward) * leg.RestFoot);
}

void RagdollBalance::DriveFreeLeg(Ragdoll& ragdoll, const BalanceLeg& leg, const JPH::Vec3 ankle, const JPH::Quat& foot)
{
	JPH::Quat thigh;
	JPH::Quat shin;
	SolveLeg(ragdoll, leg, ragdoll.mStates[leg.Thigh].Origin, ankle, thigh, shin);

	ragdoll.SetMotorTarget(leg.Thigh, mPelvisOrientation, thigh);
	ragdoll.SetMotorTarget(leg.Shin, thigh, shin);
	ragdoll.SetMotorTarget(leg.Foot, shin, foot);
}

void RagdollBalance::SolveLeg(Ragdoll& ragdoll, const BalanceLeg& leg, const JPH::Vec3 hip, const JPH::Vec3 ankle,
							  JPH::Quat& out_thigh, JPH::Quat& out_shin) const
{
	const TwoBoneIKResult ik = SolveTwoBoneIK(Vec3f::FromJolt(hip), Vec3f::FromJolt(ankle), leg.Upper, leg.Lower,
											  Vec3f::FromJolt(mForward));

	const JPH::Vec3 knee = Vec3f::ToJolt(ik.Mid);
	const JPH::Vec3 end = Vec3f::ToJolt(ik.End);

	const JPH::Vec3 thigh_direction = (knee - hip).NormalizedOr(-mFrame.Up);
	const JPH::Vec3 shin_direction = (end - knee).NormalizedOr(-mFrame.Up);
	const JPH::Vec3 leg_direction = (end - hip).NormalizedOr(-mFrame.Up);

	const JPH::Vec3 hinge = leg_direction.Cross(mForward).NormalizedOr(mFrame.Up.Cross(mForward)) * leg.HingeSign;

	ragdoll.GetHingeChainRotations(leg.Thigh, leg.Shin, thigh_direction, shin_direction, hinge, out_thigh, out_shin);
}

void RagdollBalance::ApplyTorsoControl(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, const JPH::Vec3 gravity)
{
	uint32 stance_legs[2];
	uint32 stance_count = 0;

	for (uint32 i = 0; i < 2; i++) {
		if (IsLoaded(mLegs[i])) {
			stance_legs[stance_count++] = i;
		}
	}

	if (stance_count == 0) {
		return;
	}

	JPH::Vec3 pivot = JPH::Vec3::sZero();
	float32 upper_mass = ragdoll.mStates[0].SubtreeMass;
	JPH::Vec3 upper_moment = ragdoll.mStates[0].SubtreeMoment;

	for (uint32 i = 0; i < stance_count; i++) {
		const Ragdoll::BodyState& thigh = ragdoll.mStates[mLegs[stance_legs[i]].Thigh];

		pivot += thigh.Origin;
		upper_mass -= thigh.SubtreeMass;
		upper_moment -= thigh.SubtreeMoment;
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

	float32 weights[2] = { 1.0f, 0.0f };

	if (stance_count == 2) {
		const float32 distance_a = mFrame.Horizontal(mCenterOfMass - mLegs[stance_legs[0]].FootCenter).Length();
		const float32 distance_b = mFrame.Horizontal(mCenterOfMass - mLegs[stance_legs[1]].FootCenter).Length();

		weights[0] = distance_b / std::max(distance_a + distance_b, 1e-4f);
		weights[1] = 1.0f - weights[0];
	}

	const JPH::Quat pelvis_target = mPelvisTarget;
	const float32 frequency_scale = scTorsoFrequency / ragdoll.mBodies[mLegs[0].Thigh].Muscle.Stiffness;

	for (uint32 i = 0; i < stance_count; i++) {
		const BalanceLeg& leg = mLegs[stance_legs[i]];
		const JPH::Quat thigh = ragdoll.mStates[leg.Thigh].Orientation;
		const float32 lift = MathUtil::Clamp((leg.SoleHeight - mGroundHeight - scLiftStart) / scLiftRange, 0.0f, 1.0f);
		const JPH::Quat hip_thigh = thigh.SLERP(leg.DesiredThigh, lift).Normalized();

		Ragdoll::MotorOverride& hip_motor = ragdoll.SetMotorTarget(leg.Thigh, pelvis_target, hip_thigh,
																   frequency_scale);
		hip_motor.Inertia = std::max(inertia * weights[i], 1e-2f);
	}

	const JPH::Vec3 upper_center = (upper_moment / upper_mass);

	JPH::Vec3 torque = -(upper_center - pivot).Cross(gravity * upper_mass) * scGravityFeedForward *
					   std::min(ragdoll.mBodies[0].Strength / scGravityFullStrength, 1.0f);

	const float32 torque_length = torque.Length();

	if (torque_length > scTorsoMaxTorque) {
		torque *= scTorsoMaxTorque / torque_length;
	}

	body_interface.AddTorque(ragdoll.GetBodyID(0), torque);

	for (uint32 i = 0; i < stance_count; i++) {
		body_interface.AddTorque(ragdoll.GetBodyID(mLegs[stance_legs[i]].Thigh), -torque * weights[i]);
	}
}

void RagdollBalance::ApplyCenterOfMassControl(Ragdoll& ragdoll, JPH::BodyInterface& body_interface)
{
	uint32 loaded[2];
	uint32 loaded_count = 0;

	for (uint32 i = 0; i < 2; i++) {
		if (IsLoaded(mLegs[i])) {
			loaded[loaded_count++] = i;
		}
	}

	if (loaded_count == 0) {
		return;
	}

	JPH::Vec3 target = mSupportTarget;

	if (loaded_count == 1) {
		target = mLegs[loaded[0]].FootCenter;
	}
	else if (mShiftLeg >= 0) {
		target = mLegs[1 - mShiftLeg].FootCenter;
	}

	const float32 mass = ragdoll.GetTotalMass();
	const float32 omega = 2.0f * FX_PI * scComFrequency;

	const JPH::Vec3 velocity = mFrame.Horizontal(mVelocity);
	const float32 speed = velocity.Length();
	const float32 damping_scale = -2.0f * scComDamping * omega * mass;

	JPH::Vec3 force = mFrame.Horizontal(target - mCenterOfMass) * (omega * omega * mass) + velocity * damping_scale;

	// Brakes anything above the stagger speed and never pushes, so a shove carries the body but cannot be added to
	JPH::Vec3 braking = JPH::Vec3::sZero();
	const float32 excess_speed = speed - mStaggerVelocity.Length();

	if (excess_speed > 0.0f && speed > 1e-4f) {
		braking = velocity * (excess_speed / speed * damping_scale);
	}

	const float32 strength_scale = std::min(ragdoll.mBodies[0].Strength / scGravityFullStrength, 1.0f);
	const float32 weight = mass * mFrame.Gravity;

	float32 hold = 1.0f;

	// Only holds the body while the capture point is over the feet; past that it has to step, so hard hits still fell
	// it
	if (mAssist.ReleaseDistance > 0.0f) {
		hold = MathUtil::Clamp(1.0f - mCaptureOutside / mAssist.ReleaseDistance, 0.0f, 1.0f);
	}

	hold *= 1.0f - std::min(mStaggerVelocity.Length() / scStaggerHoldSpeed, 1.0f);

	force = ClampLength(force, mAssist.MaxForceFraction * weight * strength_scale * hold);

	// Released, it only slows the body down, so each recovery step takes some speed out instead of none
	force += ClampLength(braking, mAssist.RecoveryForceFraction * weight * strength_scale) * (1.0f - hold);

	if (force.LengthSq() <= 0.0f) {
		return;
	}

	// An internal pair, pelvis against the loaded feet, so the ground still has to supply it through friction
	body_interface.AddForce(ragdoll.GetBodyID(0), force);

	for (uint32 i = 0; i < loaded_count; i++) {
		body_interface.AddForce(ragdoll.GetBodyID(mLegs[loaded[i]].Foot), -force / static_cast<float32>(loaded_count));
	}
}

} // namespace fx::physics
