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
constexpr float32 scSetDownPress = 0.01f;
/// Torque per unit spin about vertical, as a fraction of body mass, so a spinning body settles in about a second
constexpr float32 scYawDamping = 0.2f;
/// Torque per unit spin on the chest, as a fraction of body mass, for every axis
constexpr float32 scTrunkSpinDamping = 0.02f;
constexpr float32 scLiftStart = 0.003f;
constexpr float32 scLiftRange = 0.015f;

/// How far a swinging foot leaves pointing down, and arrives pointing up, for the heel to reach the ground first
constexpr float32 scSwingToeDown = MathUtil::DegToRad(15.0f);
constexpr float32 scSwingHeelUp = MathUtil::DegToRad(8.0f);
/// Fraction of the swing over which a foot levels out from leaving toes-down
constexpr float32 scSwingToeDownEnd = 0.4f;
/// Fraction of the swing where the toes start coming up for a heel landing
constexpr float32 scSwingHeelStart = 0.6f;
constexpr float32 scToeOffRoll = MathUtil::DegToRad(25.0f);
/// How far the centre of mass has to be in front of a planted foot before its heel starts to rise, and over what
/// further distance the roll completes (m)
constexpr float32 scToeOffStart = 0.08f;
constexpr float32 scToeOffRange = 0.2f;

constexpr float32 scStanceHeightFraction = 0.93f;
/// Lowest the hip sits, as a fraction of leg reach, when the foot is out of reach of the hip
constexpr float32 scMinHipFraction = 0.5f;
/// How far a swinging leg's reach is allowed to open for its landing, as a fraction of leg length
constexpr float32 scSwingReachFraction = 0.96f;
/// Lowest the stance leg lets the hip go for a swinging foot, as a fraction of that reach
constexpr float32 scMinYieldFraction = 0.75f;
/// Fraction of the swing over which the stance leg bends for it
constexpr float32 scYieldLeadFraction = 0.6f;
/// The stance knee and ankle muscles only hold posture and damp. The leg forces carry the load
constexpr float32 scStanceFrequencyScale = 0.05f;
/// Frequency (Hz) the leg forces hold the pelvis at its height
constexpr float32 scHeightFrequency = 2.0f;
constexpr float32 scMaxComShift = 0.12f;
constexpr float32 scComShiftGain = 3.0f;

constexpr float32 scTorsoFrequency = 4.0f;
constexpr float32 scTorsoMaxTorque = 300.0f;
constexpr float32 scGravityFullStrength = 0.25f;
/// Torque (N m) per unit of chest lean, reacted through the stance hips, so the upper body is pushed back upright
constexpr float32 scTrunkLeanGain = 600.0f;

/// Centre of mass control frequency (Hz) and damping ratio; its force limits come from BalanceAssist
constexpr float32 scComFrequency = 2.0f;
constexpr float32 scComDamping = 1.0f;
/// Stagger speed (m/s) at which holding the centre of mass over the feet has fully given way to following the stagger
constexpr float32 scStaggerHoldSpeed = 0.25f;
/// Most the legs push up or pull down to hold height, as a fraction of body weight. Legs can't pull on the ground, and
/// doing so takes the weight off a foot and lets it skid
constexpr float32 scMaxPushFraction = 0.6f;
constexpr float32 scMaxPullFraction = 0.15f;
constexpr uint32 scPressureIterations = 4;
/// A body this far into a fall lets its feet go
constexpr float32 scFootPlantMaxFall = 0.8f;

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

	JPH::Vec3 shift = JPH::Vec3::sZero();

	// Through a stumble the pelvis goes with the push rather than being shifted back over the feet
	if (mStumbleTime <= 0.0f) {
		shift = mGroundFrame.Horizontal(center - mCapturePoint) * scComShiftGain;
	}
	const float32 shift_length = shift.Length();

	if (shift_length > scMaxComShift) {
		shift *= scMaxComShift / shift_length;
	}

	const JPH::Vec3 pelvis_horizontal = mGroundFrame.Horizontal(ragdoll.mSegmentStates[0].Origin) + shift;

	float32 pelvis_height = std::numeric_limits<float32>::max();

	for (const BalanceLeg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		const JPH::Vec3 hip_offset = pelvis_target * leg.HipOffset;
		const float32 reach = scStanceHeightFraction * (leg.Upper + leg.Lower);
		const float32 spread = mGroundFrame.Horizontal(pelvis_horizontal + hip_offset - leg.AnklePosition).Length();

		const float32 reach2 = reach * reach;
		const float32 spread2 = spread * spread;

		const float32 hip_height = std::max(std::sqrt(std::max(reach2 - spread2, 0.0f)), reach * scMinHipFraction);

		// Measured from where the ankle sits with its sole on the floor, so a foot in the air brings the hip down until
		// it can reach rather than holding the hip up by however high the foot happens to be
		float32 ankle_height = leg.AnklePosition.Dot(mGroundFrame.Up);

		if (!IsLoaded(leg)) {
			ankle_height -= std::max(leg.SoleHeight - leg.SurfaceHeight, 0.0f);
		}

		pelvis_height = std::min(pelvis_height, ankle_height + hip_height - hip_offset.Dot(mGroundFrame.Up));
	}

	if (pelvis_height == std::numeric_limits<float32>::max()) {
		return;
	}

	// A foot reaching out for a step has to be able to get there, and the leg left standing is what brings the hip down
	// to it. Without this the hip stays high, the swinging leg straightens short of the floor and stretches for it
	if (mStatus.State == eBalanceState::Stepping) {
		const BalanceLeg& swing = mLegs[mSwing.Leg];
		const JPH::Vec3 hip_offset = pelvis_target * swing.HipOffset;
		const JPH::Quat landing_foot = GetHeadingRotation(mForward) * swing.RestFoot;
		const JPH::Vec3 landing_ankle = mSwing.Landing - landing_foot * swing.SoleCenter;

		const float32 reach = scSwingReachFraction * (swing.Upper + swing.Lower);
		const float32 spread = mGroundFrame.Horizontal(pelvis_horizontal + hip_offset - landing_ankle).Length();
		const float32 hip_height = std::max(std::sqrt(std::max(reach * reach - spread * spread, 0.0f)),
											reach * scMinYieldFraction);
		const float32 yield_height = landing_ankle.Dot(mGroundFrame.Up) + hip_height - hip_offset.Dot(mGroundFrame.Up);

		if (yield_height < pelvis_height) {
			const float32 weight = MathUtil::SmoothStep(mSwing.Time / (mSwing.Duration * scYieldLeadFraction));

			pelvis_height += (yield_height - pelvis_height) * weight;
		}
	}

	const JPH::Vec3 pelvis = pelvis_horizontal + mGroundFrame.Up * (pelvis_height - mLandingDip);

	mPelvisHeightTarget = pelvis.Dot(mGroundFrame.Up);

	for (BalanceLeg& leg : mLegs) {
		if (!leg.bStance) {
			continue;
		}

		const JPH::Vec3 hip = pelvis + pelvis_target * leg.HipOffset;

		const JPH::Vec3 foot_forward = GetHeadingForward(leg.FootOrientation, leg.RestFoot);
		JPH::Quat foot = GetHeadingRotation(foot_forward) * leg.RestFoot;

		// As the body passes over a planted foot the heel comes up and it rolls onto its toes, which lets the leg stay
		// on the ground further behind the hip
		const float32 behind = mGroundFrame.Horizontal(mCenterOfMass - leg.FootCenter).Dot(foot_forward);
		const float32 roll = scToeOffRoll * MathUtil::SmoothStep((behind - scToeOffStart) / scToeOffRange);

		if (roll > 0.0f && IsLoaded(leg)) {
			foot = PitchFoot(foot, -roll);
		}

		if (!IsLoaded(leg)) {
			// Only just below the floor, or the foot lands hard enough to bounce and has to come back down again
			const JPH::Vec3 ankle = leg.AnklePosition -
									mGroundFrame.Up *
										(std::max(leg.SoleHeight - leg.SurfaceHeight, 0.0f) + scSetDownPress);
			DriveFreeLeg(ragdoll, leg, ankle, foot);
			continue;
		}

		const JPH::Vec3 ankle = leg.AnklePosition -
								mGroundFrame.Up * (std::max(leg.SoleHeight - leg.SurfaceHeight, 0.0f) + scStancePress);

		JPH::Quat thigh;
		JPH::Quat shin;
		SolveLeg(ragdoll, leg, hip, ankle, thigh, shin);

		leg.DesiredThigh = thigh;

		// Knee and ankle carry the body above them, so their stiffness comes from the supported inertia
		ragdoll.SetMotorTarget(leg.Shin, thigh, shin, scStanceFrequencyScale, true);
		ragdoll.SetMotorTarget(leg.Foot, shin, foot, scStanceFrequencyScale, true);
		ragdoll.mMotorOverrides[leg.Foot].bKeepFootPitch = (roll > 0.0f);

		ragdoll.mMotorOverrides[leg.Thigh].bNoGravityComp = true;
		ragdoll.mMotorOverrides[leg.Shin].bNoGravityComp = true;
		ragdoll.mMotorOverrides[leg.Foot].bNoGravityComp = true;
	}
}

JPH::Quat RagdollBalance::PitchFoot(const JPH::Quat& foot, float32 radians) const
{
	const JPH::Vec3 toe_up_axis = mForward.Cross(mGroundFrame.Up).NormalizedOr(JPH::Vec3::sAxisX());

	return (JPH::Quat::sRotation(toe_up_axis, radians) * foot).Normalized();
}

void RagdollBalance::DriveSwingLeg(Ragdoll& ragdoll)
{
	const BalanceLeg& leg = mLegs[mSwing.Leg];
	const float32 phase = MathUtil::Clamp(mSwing.Time / mSwing.Duration, 0.0f, 1.0f);

	// The toes leave the ground last, and the heel reaches it first
	const float32 toe_down = scSwingToeDown * (1.0f - MathUtil::SmoothStep(phase / scSwingToeDownEnd));
	const float32 heel_first = scSwingHeelUp *
							   MathUtil::SmoothStep((phase - scSwingHeelStart) / (1.0f - scSwingHeelStart));
	const JPH::Quat foot = PitchFoot(GetHeadingRotation(mForward) * leg.RestFoot, heel_first - toe_down);

	DriveFreeLeg(ragdoll, leg, mSwing.AnkleTarget, foot);

	ragdoll.mMotorOverrides[leg.Foot].bKeepFootPitch = true;
}

void RagdollBalance::DriveHead(Ragdoll& ragdoll)
{
	const uint32 neck = mpRig->Neck;
	const RDSegmentInfo& info = ragdoll.mSegments[neck];
	const JPH::Quat chest = ragdoll.mSegmentStates[info.ParentBody].Orientation;
	const JPH::Quat held = (chest * ragdoll.mSegmentTargets[info.ParentBody].Orientation.Conjugated() *
							ragdoll.mSegmentTargets[neck].Orientation)
							   .Normalized();

	// The eyes stay level whatever the body does: what the neck was holding, with the chest's tip taken back out
	const JPH::Vec3 chest_up = (chest * ragdoll.mSegments[mpRig->Chest].RestOrientation.Conjugated()) * mGroundFrame.Up;
	const JPH::Quat level = (JPH::Quat::sFromTo(chest_up, mGroundFrame.Up) * held).Normalized();

	ragdoll.SetMotorTarget(neck, chest, level);
}

void RagdollBalance::DriveFreeLeg(Ragdoll& ragdoll, const BalanceLeg& leg, const JPH::Vec3 ankle, const JPH::Quat& foot)
{
	JPH::Quat thigh;
	JPH::Quat shin;
	SolveLeg(ragdoll, leg, ragdoll.mSegmentStates[leg.Thigh].Origin, ankle, thigh, shin);

	ragdoll.SetMotorTarget(leg.Thigh, mPelvisOrientation, thigh).bPrecise = true;
	ragdoll.SetMotorTarget(leg.Shin, thigh, shin).bPrecise = true;
	// Against the shin as it is, not as it was asked to be, or the foot tips by however far the shin is lagging
	ragdoll.SetMotorTarget(leg.Foot, ragdoll.mSegmentStates[leg.Shin].Orientation, foot).bPrecise = true;
}

void RagdollBalance::SolveLeg(Ragdoll& ragdoll, const BalanceLeg& leg, const JPH::Vec3 hip, const JPH::Vec3 ankle,
							  JPH::Quat& out_thigh, JPH::Quat& out_shin) const
{
	const TwoBoneIKResult ik = SolveTwoBoneIK(Vec3f::FromJolt(hip), Vec3f::FromJolt(ankle), leg.Upper, leg.Lower,
											  Vec3f::FromJolt(mForward));

	const JPH::Vec3 knee = Vec3f::ToJolt(ik.Mid);
	const JPH::Vec3 end = Vec3f::ToJolt(ik.End);

	const JPH::Vec3 thigh_direction = (knee - hip).NormalizedOr(-mGroundFrame.Up);
	const JPH::Vec3 shin_direction = (end - knee).NormalizedOr(-mGroundFrame.Up);
	const JPH::Vec3 leg_direction = (end - hip).NormalizedOr(-mGroundFrame.Up);

	const JPH::Vec3 hinge = leg_direction.Cross(mForward).NormalizedOr(mGroundFrame.Up.Cross(mForward)) * leg.HingeSign;

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
	float32 upper_mass = ragdoll.mSegmentStates[0].SubtreeMass;
	JPH::Vec3 upper_moment = ragdoll.mSegmentStates[0].SubtreeMoment;

	for (uint32 i = 0; i < stance_count; i++) {
		const RDSegmentState& thigh = ragdoll.mSegmentStates[mLegs[stance_legs[i]].Thigh];

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
		const float32 distance_a = mGroundFrame.Horizontal(mCenterOfMass - mLegs[stance_legs[0]].FootCenter).Length();
		const float32 distance_b = mGroundFrame.Horizontal(mCenterOfMass - mLegs[stance_legs[1]].FootCenter).Length();

		weights[0] = distance_b / std::max(distance_a + distance_b, 1e-4f);
		weights[1] = 1.0f - weights[0];
	}

	const JPH::Quat pelvis_target = mPelvisTarget;

	for (uint32 i = 0; i < stance_count; i++) {
		const BalanceLeg& leg = mLegs[stance_legs[i]];
		const float32 frequency_scale = scTorsoFrequency / ragdoll.mSegments[leg.Thigh].Muscle.Stiffness;
		const JPH::Quat thigh = ragdoll.mSegmentStates[leg.Thigh].Orientation;
		const float32 lift = MathUtil::Clamp((leg.SoleHeight - leg.SurfaceHeight - scLiftStart) / scLiftRange, 0.0f,
											 1.0f);
		const JPH::Quat hip_thigh = thigh.SLERP(leg.DesiredThigh, lift).Normalized();

		Ragdoll::MotorOverride& hip_motor = ragdoll.SetMotorTarget(leg.Thigh, pelvis_target, hip_thigh,
																   frequency_scale);
		hip_motor.Inertia = std::max(inertia * weights[i], 1e-2f);
	}

	JPH::Vec3 torque = JPH::Vec3::sZero();

	const JPH::Quat chest = ragdoll.mSegmentStates[mpRig->Chest].Orientation;
	const JPH::Vec3 chest_up = (chest * ragdoll.mSegments[mpRig->Chest].RestOrientation.Conjugated()) * mGroundFrame.Up;
	torque += chest_up.Cross(mGroundFrame.Up) * scTrunkLeanGain;

	const float32 torque_length = torque.Length();

	if (torque_length > scTorsoMaxTorque) {
		torque *= scTorsoMaxTorque / torque_length;
	}

	torque *= 1.0f - mStatus.FallProgress;

	body_interface.AddTorque(ragdoll.GetBodyID(0), torque);

	for (uint32 i = 0; i < stance_count; i++) {
		body_interface.AddTorque(ragdoll.GetBodyID(mLegs[stance_legs[i]].Thigh), -torque * weights[i]);
	}
}

JPH::Vec3 RagdollBalance::GetCenterOfMassForce(uint32 loaded_count, const uint32* loaded, float32 mass,
											   float32 strength_scale) const
{
	const float32 weight = mass * mGroundFrame.Gravity;

	// The hold and braking wait for recovery, so through the stumble the shove carries the body on its own
	if (mStumbleTime > 0.0f) {
		return JPH::Vec3::sZero();
	}

	JPH::Vec3 target = mSupportTarget;

	if (loaded_count == 1) {
		target = mLegs[loaded[0]].FootCenter;
	}
	else if (mShiftLeg >= 0) {
		target = mLegs[1 - mShiftLeg].FootCenter;
	}

	const float32 omega = 2.0f * FX_PI * scComFrequency;

	const JPH::Vec3 velocity = mGroundFrame.Horizontal(mVelocity);
	const float32 speed = velocity.Length();
	const float32 damping_scale = -2.0f * scComDamping * omega * mass;

	JPH::Vec3 force = mGroundFrame.Horizontal(target - mCenterOfMass) * (omega * omega * mass) +
					  velocity * damping_scale;

	JPH::Vec3 braking = JPH::Vec3::sZero();
	const float32 excess_speed = speed - mStaggerVelocity.Length();

	if (excess_speed > 0.0f && speed > 1e-4f) {
		braking = velocity * (excess_speed / speed * damping_scale);
	}

	float32 hold = 1.0f;

	// Only hold the body when the capture point is over where the feet are. If it is past the feet, the body needs to
	// step
	if (mAssist.ReleaseDistance > 0.0f) {
		hold = MathUtil::Clamp(1.0f - mCaptureOutside / mAssist.ReleaseDistance, 0.0f, 1.0f);
	}

	hold *= 1.0f - std::min(mStaggerVelocity.Length() / scStaggerHoldSpeed, 1.0f);

	force = ClampLength(force, mAssist.MaxForceFraction * weight * strength_scale * hold);

	// Released, it only slows the body down, so each recovery step takes some speed out instead of none
	force += ClampLength(braking, mAssist.RecoveryForceFraction * weight * strength_scale) * (1.0f - hold);

	return force * (1.0f - mStatus.FallProgress);
}

JPH::Vec3 RagdollBalance::LimitAnkleTorque(const BalanceLeg& leg, const JPH::Vec3 torque, float32 normal_force) const
{
	if (leg.ContactCount == 0 || normal_force <= 0.0f) {
		return JPH::Vec3::sZero();
	}

	Vec2f points[BodyGroundContact::scMaxPoints];
	Vec2f hull[BodyGroundContact::scMaxPoints + 1];

	for (uint32 i = 0; i < leg.ContactCount; i++) {
		points[i] = mGroundFrame.ToPlane(leg.ContactPoints[i]);
	}

	const uint32 count = BuildHull(points, leg.ContactCount, hull);

	// The foot can only push back on the ankle by moving its centre of pressure, which stays under the sole
	const JPH::Vec3 pressure = mGroundFrame.Up.Cross(torque) / normal_force;
	const Vec2f ankle = mGroundFrame.ToPlane(leg.AnklePosition);
	const Vec2f wanted = ankle + mGroundFrame.ToPlane(pressure);

	float32 distance = 0.0f;
	const Vec2f closest = ClosestOnHull(hull, count, wanted, distance);

	if (distance <= 0.0f) {
		return mGroundFrame.Horizontal(torque);
	}

	const Vec2f limited = closest - ankle;
	const JPH::Vec3 limited_pressure = mGroundFrame.AxisU * limited.X + mGroundFrame.AxisV * limited.Y;

	return limited_pressure.Cross(mGroundFrame.Up) * normal_force;
}

bool RagdollBalance::IsPressureInside(const BalanceLeg& leg, const JPH::Vec3 torque, float32 normal_force) const
{
	if (leg.ContactCount == 0 || normal_force <= 0.0f) {
		return false;
	}

	Vec2f points[BodyGroundContact::scMaxPoints];
	Vec2f hull[BodyGroundContact::scMaxPoints + 1];

	for (uint32 i = 0; i < leg.ContactCount; i++) {
		points[i] = mGroundFrame.ToPlane(leg.ContactPoints[i]);
	}

	const uint32 count = BuildHull(points, leg.ContactCount, hull);
	const Vec2f wanted = mGroundFrame.ToPlane(leg.AnklePosition) +
						 mGroundFrame.ToPlane(mGroundFrame.Up.Cross(torque) / normal_force);

	float32 distance = 0.0f;
	ClosestOnHull(hull, count, wanted, distance);

	return distance <= 0.0f;
}

void RagdollBalance::ApplyJointTorque(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, uint32 body,
									  const JPH::Vec3 torque)
{
	body_interface.AddTorque(ragdoll.GetBodyID(static_cast<uint32>(ragdoll.mSegments[body].ParentBody)), torque);
	body_interface.AddTorque(ragdoll.GetBodyID(body), -torque);
}

void RagdollBalance::ApplyExternalSupport(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, bool loaded)
{
	// Slow to let go, so a foot's contact flicker can't pulse the support
	constexpr float32 scLiftAttackTime = 0.1f;
	constexpr float32 scLiftReleaseTime = 0.2f;
	constexpr float32 scLiftFullCapture = 0.1f;
	constexpr float32 scDampingAnchorTime = 0.25f;
	constexpr float32 scMaxDampingFraction = 0.2f;

	const float32 mass = ragdoll.GetTotalMass();
	const float32 weight = mass * mGroundFrame.Gravity;
	const float32 strength_scale = std::min(ragdoll.mSegments[0].Strength / scGravityFullStrength, 1.0f);
	const RDSegmentState& root = ragdoll.mSegmentStates[0];
	const JPH::BodyID root_id = ragdoll.GetBodyID(0);

	float32 lift_target = 0.0f;

	if (loaded && IsViable()) {
		float32 need = MathUtil::Clamp(mCaptureOutside / scLiftFullCapture, 0.0f, 1.0f);

		if (mStatus.State == eBalanceState::Stepping) {
			need = 1.0f;
		}

		lift_target = mAssist.LiftFraction * need * strength_scale * (1.0f - mStatus.FallProgress);
	}

	float32 lift_time = scLiftReleaseTime;

	if (lift_target > mLift) {
		lift_time = scLiftAttackTime;
	}

	mLift += (lift_target - mLift) * (1.0f - std::exp(-mDeltaTime / lift_time));

	if (mLift > 0.0f) {
		const JPH::Vec3 force = mGroundFrame.Up * (mLift * weight);

		// Through the centre of mass, so holding the body up doesn't also turn it
		body_interface.AddForce(root_id, force);
		body_interface.AddTorque(root_id, (mCenterOfMass - root.Center).Cross(force));
		mStatus.AssistForce += force.Length();
	}

	if (mAssist.PelvisDamping <= 0.0f) {
		return;
	}

	if (!mbHasSmoothedPelvisVelocity || !loaded) {
		mSmoothedPelvisVelocity = root.Velocity;
		mbHasSmoothedPelvisVelocity = true;
		return;
	}

	mSmoothedPelvisVelocity += (root.Velocity - mSmoothedPelvisVelocity) *
							   (1.0f - std::exp(-mDeltaTime / scDampingAnchorTime));

	// Damps against the pelvis's own recent motion rather than a fixed point, so it doesn't take back a knock
	const JPH::Vec3 damping = ClampLength((mSmoothedPelvisVelocity - root.Velocity) * (mAssist.PelvisDamping * mass),
										  scMaxDampingFraction * weight) *
							  (strength_scale * (1.0f - mStatus.FallProgress));

	body_interface.AddForce(root_id, damping);
	mStatus.AssistForce += damping.Length();
}

void RagdollBalance::ApplyLegForces(Ragdoll& ragdoll, JPH::BodyInterface& body_interface)
{
	uint32 loaded[2];
	uint32 loaded_count = 0;

	mStatus.LegForce = 0.0f;
	mStatus.AssistForce = 0.0f;

	for (uint32 i = 0; i < 2; i++) {
		if (IsLoaded(mLegs[i])) {
			loaded[loaded_count++] = i;
		}
	}

	ApplyExternalSupport(ragdoll, body_interface, loaded_count > 0);

	if (loaded_count == 0) {
		return;
	}

	const float32 mass = ragdoll.GetTotalMass();
	// What the lift carries is off the legs
	const float32 gravity = mGroundFrame.Gravity * (1.0f - mLift);
	const float32 weight = mass * mGroundFrame.Gravity;
	const float32 strength_scale = std::min(ragdoll.mSegments[0].Strength / scGravityFullStrength, 1.0f);
	const JPH::Vec3 up = mGroundFrame.Up;

	const JPH::Vec3 horizontal = GetCenterOfMassForce(loaded_count, loaded, mass, strength_scale);

	// Everything above the loaded legs, which they share. A swinging leg hangs from the pelvis, so it counts
	const RDSegmentState& root = ragdoll.mSegmentStates[0];
	float32 upper_mass = root.SubtreeMass;
	JPH::Vec3 upper_moment = root.SubtreeMoment;

	for (uint32 i = 0; i < loaded_count; i++) {
		const RDSegmentState& thigh = ragdoll.mSegmentStates[mLegs[loaded[i]].Thigh];

		upper_mass -= thigh.SubtreeMass;
		upper_moment -= thigh.SubtreeMoment;
	}

	const float32 height_omega = 2.0f * FX_PI * scHeightFrequency;
	const float32 height_error = mPelvisHeightTarget - root.Origin.Dot(up);
	const float32 climb = ragdoll.mSegmentStates[0].Velocity.Dot(up);

	float32 vertical = mass * (height_omega * height_omega * height_error - 2.0f * height_omega * climb);
	vertical = MathUtil::Clamp(vertical, -scMaxPullFraction * weight, scMaxPushFraction * weight) * strength_scale;

	float32 total_transmitted = 0.0f;

	for (uint32 i = 0; i < loaded_count; i++) {
		const BalanceLeg& leg = mLegs[loaded[i]];

		float32 share = 1.0f;

		if (loaded_count == 2) {
			share = GetWeightShare(loaded[i]);
		}

		const RDSegmentState& thigh = ragdoll.mSegmentStates[leg.Thigh];

		const float32 own_mass = thigh.SubtreeMass;
		const float32 normal_force = std::max((share * upper_mass + own_mass) * gravity + share * vertical, 0.0f);

		// The ground can't push sideways harder than friction lets it
		const JPH::Vec3 wanted_force = ClampLength(horizontal * share, leg.ContactFriction * normal_force) +
									   up * (share * vertical);

		const auto get_joint_torque = [&](const RDSegmentState& lower, const JPH::Vec3 force)
		{
			// What this joint holds up: this leg's share of the upper body, and whatever of the leg is above the joint
			const float32 above_mass = share * upper_mass + (own_mass - lower.SubtreeMass);
			const JPH::Vec3 above_moment = upper_moment * share + (thigh.SubtreeMoment - lower.SubtreeMoment);

			return (above_moment - lower.Origin * above_mass).Cross(up * gravity) * strength_scale +
				   (mCenterOfMass - lower.Origin).Cross(force);
		};

		// A force through the centre of mass has to come up through the centre of pressure, so the sole's size limits
		// it. All of the leg gets the same reduced force, or the joints above the ankle would pitch the body over
		const RDSegmentState& foot_state = ragdoll.mSegmentStates[leg.Foot];
		float32 low = 0.0f;
		float32 high = 1.0f;

		if (!IsPressureInside(leg, get_joint_torque(foot_state, wanted_force), normal_force)) {
			for (uint32 iteration = 0; iteration < scPressureIterations; iteration++) {
				const float32 middle = (low + high) * 0.5f;

				if (IsPressureInside(leg, get_joint_torque(foot_state, wanted_force * middle), normal_force)) {
					low = middle;
				}
				else {
					high = middle;
				}
			}

			high = low;
		}

		const JPH::Vec3 leg_force = wanted_force * high;
		total_transmitted += mGroundFrame.Horizontal(leg_force).Length();

		const JPH::Vec3 unsupported = mGroundFrame.Horizontal(wanted_force - leg_force);

		const uint32 joints[3] = { leg.Thigh, leg.Shin, leg.Foot };

		for (const uint32 joint : joints) {
			const RDSegmentState& lower = ragdoll.mSegmentStates[joint];

			JPH::Vec3 torque = get_joint_torque(lower, leg_force);

			if (joint == leg.Foot) {
				torque = LimitAnkleTorque(leg, torque, normal_force);
			}
			else if (joint == leg.Shin) {
				const JPH::Vec3 hinge = (lower.Orientation * ragdoll.mSegments[joint].HingeLocal).NormalizedOr(up);
				torque = hinge * torque.Dot(hinge);
			}

			const float32 limit = ragdoll.mSegments[joint].Muscle.MaxTorque * strength_scale;

			ApplyJointTorque(ragdoll, body_interface, joint, ClampLength(torque, limit));
		}

		// Whatever the sole could not carry still reaches the pelvis as a pair with the foot, which friction limits. It
		// skips the joints and the sole's limit, so the aim is for better stepping to make it unnecessary
		const float32 horizontal_force = mGroundFrame.Horizontal(leg_force).Length();
		const float32 residual_limit = std::min(mAssist.ResidualFraction * weight * share * strength_scale,
												std::max(leg.ContactFriction * normal_force - horizontal_force, 0.0f));
		const JPH::Vec3 residual = ClampLength(unsupported, residual_limit);

		body_interface.AddForce(ragdoll.GetBodyID(0), residual);
		if (mAssist.ExternalResidual <= 0.0f) {
			body_interface.AddForce(ragdoll.GetBodyID(leg.Foot), -residual);
		}

		mStatus.AssistForce += residual.Length();

		// The ankle's muscle shares the sole's limited leverage
		ragdoll.mMotorOverrides[leg.Foot].MaxTorque = normal_force * mpRig->FootHalfLength;

		// Loaded rather than touching, since a flat foot's contact flickers and each loss would let go of its spot
		if (mStatus.FallProgress < scFootPlantMaxFall) {
			ragdoll.RequestFootPlant(loaded[i], normal_force, leg.ContactFriction,
									 !ragdoll.mMotorOverrides[leg.Foot].bKeepFootPitch);
		}
	}

	mStatus.LegForce = total_transmitted;
}

void RagdollBalance::ApplyTrunkSpinDamping(Ragdoll& ragdoll, JPH::BodyInterface& body_interface)
{
	const float32 mass = ragdoll.GetTotalMass();

	// The pelvis is damped about vertical, so a spin there settles instead of carrying on
	const JPH::Vec3 pelvis_spin = body_interface.GetAngularVelocity(ragdoll.GetBodyID(0));
	const JPH::Vec3 pelvis_torque = mGroundFrame.Up * (-scYawDamping * mass * pelvis_spin.Dot(mGroundFrame.Up));

	uint32 loaded_count = 0;

	for (const BalanceLeg& leg : mLegs) {
		loaded_count += IsLoaded(leg) ? 1 : 0;
	}

	// The ground takes the reaction through the loaded feet, and with none down there is nothing to push against
	if (loaded_count > 0) {
		body_interface.AddTorque(ragdoll.GetBodyID(0), pelvis_torque);

		for (const BalanceLeg& leg : mLegs) {
			if (IsLoaded(leg)) {
				body_interface.AddTorque(ragdoll.GetBodyID(leg.Foot),
										 -pelvis_torque / static_cast<float32>(loaded_count));
			}
		}
	}

	// The chest is damped about every axis against the pelvis, so the upper body stops rocking after a shove
	const JPH::BodyID chest_id = ragdoll.GetBodyID(mpRig->Chest);
	const JPH::Vec3 chest_spin = body_interface.GetAngularVelocity(chest_id) - pelvis_spin;
	const JPH::Vec3 chest_torque = -chest_spin * (scTrunkSpinDamping * mass);

	body_interface.AddTorque(chest_id, chest_torque);
	body_interface.AddTorque(ragdoll.GetBodyID(0), -chest_torque);
}

} // namespace fx::physics
