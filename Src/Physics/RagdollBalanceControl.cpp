#include "Ragdoll.hpp"
#include "RagdollBalance.hpp"

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

constexpr float32 scStanceHeightFraction = 0.95f;
constexpr float32 scMinHipHeight = 0.25f;
constexpr float32 scStanceFrequencyScale = 0.15f;
constexpr float32 scMaxComShift = 0.12f;
constexpr float32 scComShiftGain = 3.0f;

constexpr float32 scTorsoFrequency = 4.0f;
constexpr float32 scTorsoMaxTorque = 300.0f;
constexpr float32 scGravityFeedForward = 1.0f;
constexpr float32 scGravityFullStrength = 0.25f;

} // namespace

bool RagdollBalance::IsLoaded(const RagdollLeg& leg) const { return leg.bStance && leg.ContactAge < scContactHoldTime; }

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

	const JPH::Vec3 pelvis_horizontal = Horizontal(ragdoll.mStates[0].Origin) + shift;

	float32 pelvis_height = std::numeric_limits<float32>::max();

	for (const RagdollLeg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		const JPH::Vec3 hip_offset = pelvis_target * leg.HipOffset;
		const float32 reach = scStanceHeightFraction * (leg.Upper + leg.Lower);
		const float32 spread = Horizontal(pelvis_horizontal + hip_offset - leg.AnklePosition).Length();

		const float32 reach2 = reach * reach;
		const float32 spread2 = spread * spread;


		const float32 hip_height = std::sqrt(std::max(reach2 - spread2, reach2 * scMinHipHeight));

		pelvis_height = std::min(pelvis_height, leg.AnklePosition.Dot(mUp) + hip_height - hip_offset.Dot(mUp));
	}

	if (pelvis_height == std::numeric_limits<float32>::max()) {
		return;
	}

	const JPH::Vec3 pelvis = pelvis_horizontal + mUp * pelvis_height;

	for (RagdollLeg& leg : mLegs) {
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

		ragdoll.mMotorOverrides[leg.Thigh].bNoGravityComp = true;
		ragdoll.mMotorOverrides[leg.Shin].bNoGravityComp = true;
		ragdoll.mMotorOverrides[leg.Foot].bNoGravityComp = true;
	}
}

void RagdollBalance::DriveSwingLeg(Ragdoll& ragdoll)
{
	const RagdollLeg& leg = mLegs[mSwing.Leg];

	DriveFreeLeg(ragdoll, leg, mSwing.AnkleTarget, GetHeadingRotation(mForward) * leg.RestFoot);
}

void RagdollBalance::DriveFreeLeg(Ragdoll& ragdoll, const RagdollLeg& leg, const JPH::Vec3 ankle, const JPH::Quat& foot)
{
	JPH::Quat thigh;
	JPH::Quat shin;
	SolveLeg(ragdoll, leg, ragdoll.mStates[leg.Thigh].Origin, ankle, thigh, shin);

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

void RagdollBalance::SolveLeg(Ragdoll& ragdoll, const RagdollLeg& leg, const JPH::Vec3 hip, const JPH::Vec3 ankle,
							  JPH::Quat& out_thigh, JPH::Quat& out_shin) const
{
	const TwoBoneIKResult ik = SolveTwoBoneIK(Vec3f::FromJolt(hip), Vec3f::FromJolt(ankle), leg.Upper, leg.Lower,
											  Vec3f::FromJolt(mForward));

	const JPH::Vec3 knee = Vec3f::ToJolt(ik.Mid);
	const JPH::Vec3 end = Vec3f::ToJolt(ik.End);

	const JPH::Vec3 thigh_direction = (knee - hip).NormalizedOr(-mUp);
	const JPH::Vec3 shin_direction = (end - knee).NormalizedOr(-mUp);
	const JPH::Vec3 leg_direction = (end - hip).NormalizedOr(-mUp);

	const JPH::Vec3 hinge = leg_direction.Cross(mForward).NormalizedOr(mUp.Cross(mForward)) * leg.HingeSign;

	const Ragdoll::BodyInfo& thigh = ragdoll.mBodies[leg.Thigh];
	const Ragdoll::BodyInfo& shin = ragdoll.mBodies[leg.Shin];

	out_thigh = FrameRotation(thigh.LocalAxis, shin.ParentHingeLocal, thigh_direction, hinge);
	out_shin = FrameRotation(shin.LocalAxis, shin.HingeLocal, shin_direction, hinge);
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
		const float32 distance_a = Horizontal(mCenterOfMass - mLegs[stance_legs[0]].FootCenter).Length();
		const float32 distance_b = Horizontal(mCenterOfMass - mLegs[stance_legs[1]].FootCenter).Length();

		weights[0] = distance_b / std::max(distance_a + distance_b, 1e-4f);
		weights[1] = 1.0f - weights[0];
	}

	const JPH::Quat pelvis_target = ragdoll.mTargets[0].Orientation;
	const float32 frequency_scale = scTorsoFrequency / ragdoll.mBodies[mLegs[0].Thigh].Muscle.Stiffness;

	for (uint32 i = 0; i < stance_count; i++) {
		const RagdollLeg& leg = mLegs[stance_legs[i]];
		const JPH::Quat thigh = body_interface.GetRotation(ragdoll.GetBodyID(leg.Thigh));
		const float32 lift = std::clamp((leg.SoleHeight - mGroundHeight - scLiftStart) / scLiftRange, 0.0f, 1.0f);
		const JPH::Quat hip_thigh = thigh.SLERP(leg.DesiredThigh, lift).Normalized();

		Ragdoll::MotorOverride& hip_motor = ragdoll.mMotorOverrides[leg.Thigh];

		hip_motor.Mode = Ragdoll::eMotorOverride::Target;
		hip_motor.Target = (pelvis_target.Conjugated() * hip_thigh).Normalized();
		hip_motor.FrequencyScale = frequency_scale;
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

} // namespace fx::physics
