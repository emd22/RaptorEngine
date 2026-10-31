#include "Ragdoll.hpp"
#include "RagdollMath.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <algorithm>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scMotorMinFrequencyScale = 0.2f;
/// How far the muscles let go as a fall completes: at full fall progress strength drops to 1 minus this
constexpr float32 scFallLimp = 0.65f;
constexpr float32 scMinSubtreeInertia = 1e-4f;
constexpr float32 scGravityCompFullStrength = 0.25f;
constexpr float32 scOverrideFadeTime = 0.4f;

constexpr float32 scRootAssistFrequency = 6.0f;
constexpr float32 scRootAssistDamping = 1.0f;
constexpr float32 scRootAssistMaxForce = 1500.0f;
constexpr float32 scRootAssistMaxTorque = 600.0f;

/// Spring with natural frequency `omega` and damping ratio `damping` for a `mass` (or moment of inertia), times `scale`
JPH::SpringSettings MakeSpring(float32 mass, float32 omega, float32 damping, float32 scale = 1.0f)
{
	return JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, scale * mass * omega * omega,
							   scale * 2.0f * damping * omega * mass);
}

/// How far ahead to aim a damped spring along the target's velocity so it tracks without lag (2 zeta / omega)
float32 GetLeadTime(float32 damping, float32 omega) { return 2.0f * damping / omega; }

} // namespace

void Ragdoll::OnStep(const JPH::PhysicsStepListenerContext& inContext)
{
	if (mpRagdoll == nullptr) {
		return;
	}

	JPH::BodyInterface& body_interface = inContext.mPhysicsSystem->GetBodyInterfaceNoLock();

	if (inContext.mIsFirstStep) {
		mSubstep = 0;
		StorePreviousPoses(body_interface);
	}

	const uint32 steps_left = std::max(static_cast<uint32>(JoltPhysicsBackend::scCollisionSteps) - mSubstep, 1u);
	float32 fraction = 1.0f;

	if (!inContext.mIsLastStep) {
		fraction = 1.0f / static_cast<float32>(steps_left);
	}

	mSubstep++;

	MoveKinematicBodies(body_interface, fraction, inContext.mDeltaTime);

	const JPH::Vec3 gravity = inContext.mPhysicsSystem->GetGravity();

	// Ground contacts are cleared before each frame's update and filled during collision steps, so on the first step
	// after a clear they are empty and the balance controller keeps its previous contact state
	const uint32 contact_generation = gPhysics->pBackend->GetGroundContactGeneration();
	const bool contacts_fresh = (contact_generation == mContactGeneration);
	mContactGeneration = contact_generation;

	if (!mbLimp) {
		UpdateSubtrees(body_interface);

		if (mbBalancing) {
			mBalance.Step(*this, body_interface, inContext.mDeltaTime, gravity, contacts_fresh);
		}
	}

	for (uint32 i = 0; i < mSegments.Size; i++) {
		ApplyMotor(i);
	}

	if (!mbBalancing && mOverrideFade > 0.0f) {
		mOverrideFade = std::max(mOverrideFade - inContext.mDeltaTime / scOverrideFadeTime, 0.0f);
	}

	if (mbLimp) {
		if (mpRootAssist != nullptr) {
			mpRootAssist->SetEnabled(false);
		}

		return;
	}

	ApplyGravityCompensation(body_interface, gravity);
	ApplyRootAssist(body_interface, gravity);
}

void Ragdoll::MoveKinematicBodies(JPH::BodyInterface& body_interface, float32 fraction, float32 delta_time)
{
	for (uint32 i = 0; i < mSegments.Size; i++) {
		if (mSegments[i].Mode != eRagdollBodyMode::Kinematic) {
			continue;
		}

		const RDSegmentTarget& target = mSegmentTargets[i];
		const JPH::BodyID body_id = GetBodyID(i);

		JPH::RVec3 position;
		JPH::Quat orientation;
		body_interface.GetPositionAndRotation(body_id, position, orientation);

		const JPH::Vec3 step_position = JPH::Vec3(position) + (target.Position - JPH::Vec3(position)) * fraction;
		const JPH::Quat step_orientation = orientation.SLERP(target.Orientation, fraction).Normalized();

		body_interface.MoveKinematic(body_id, step_position, step_orientation, delta_time);
	}
}

void Ragdoll::UpdateSubtrees(JPH::BodyInterface& body_interface)
{
	for (uint32 i = 0; i < mSegments.Size; i++) {
		const RDSegmentInfo& info = mSegments[i];
		RDSegmentState& state = mSegmentStates[i];

		SampleSegment(body_interface, i, state);

		float32 mass = 0.0f;
		float32 inertia = 0.0f;

		if (info.Mode != eRagdollBodyMode::Kinematic) {
			mass = info.Mass;
		}

		if (mass > 0.0f) {
			inertia = info.Inertia;
		}

		state.SubtreeMass = mass;
		state.SubtreeMoment = state.Center * mass;
		state.SubtreeSecondMoment = mass * state.Center.LengthSq() + inertia;
	}

	for (uint32 i = mSegments.Size - 1; i > 0; i--) {
		const RDSegmentState& child = mSegmentStates[i];
		RDSegmentState& parent = mSegmentStates[mSegments[i].ParentBody];

		parent.SubtreeMass += child.SubtreeMass;
		parent.SubtreeMoment += child.SubtreeMoment;
		parent.SubtreeSecondMoment += child.SubtreeSecondMoment;
	}
}

float32 Ragdoll::GetRawSubtreeInertia(uint32 body_index, const JPH::Vec3 pivot) const
{
	const RDSegmentState& state = mSegmentStates[body_index];

	return state.SubtreeSecondMoment - 2.0f * pivot.Dot(state.SubtreeMoment) + state.SubtreeMass * pivot.LengthSq();
}

float32 Ragdoll::GetSubtreeInertia(uint32 body_index, const JPH::Vec3 pivot) const
{
	return std::max(GetRawSubtreeInertia(body_index, pivot), scMinSubtreeInertia);
}

float32 Ragdoll::GetSupportedInertia(uint32 body_index, const JPH::Vec3 pivot) const
{
	const float32 inertia = GetRawSubtreeInertia(0, pivot) - GetRawSubtreeInertia(body_index, pivot);

	return std::max(inertia, scMinSubtreeInertia);
}

JPH::SwingTwistConstraint* Ragdoll::GetJoint(uint32 body_index) const
{
	// Jolt orders constraints by child part, and the root part has none
	return static_cast<JPH::SwingTwistConstraint*>(mpRagdoll->GetConstraint(static_cast<int>(body_index) - 1));
}

Ragdoll::MotorOverride& Ragdoll::SetMotorTarget(uint32 body_index, const JPH::Quat& parent_world,
												const JPH::Quat& child_world, float32 frequency_scale, bool supported)
{
	MotorOverride& motor_override = mMotorOverrides[body_index];

	motor_override.Mode = eMotorOverride::Target;
	motor_override.Target = RelativeRotation(parent_world, child_world);
	motor_override.FrequencyScale = frequency_scale;
	motor_override.bSupported = supported;

	return motor_override;
}

void Ragdoll::GetHingeChainRotations(uint32 upper, uint32 lower, const JPH::Vec3 upper_direction,
									 const JPH::Vec3 lower_direction, const JPH::Vec3 hinge, JPH::Quat& out_upper,
									 JPH::Quat& out_lower) const
{
	const RDSegmentInfo& upper_body = mSegments[upper];
	const RDSegmentInfo& lower_body = mSegments[lower];

	out_upper = FrameRotation(upper_body.LocalAxis, lower_body.ParentHingeLocal, upper_direction, hinge);
	out_lower = FrameRotation(lower_body.LocalAxis, lower_body.HingeLocal, lower_direction, hinge);
}

JPH::Quat Ragdoll::LevelFoot(uint32 body_index, const JPH::Quat& relative) const
{
	const RDSegmentInfo& info = mSegments[body_index];
	const JPH::Quat parent = mSegmentStates[info.ParentBody].Orientation;
	const JPH::Quat foot = (parent * relative).Normalized();

	// The collider is the foot box, so its up axis has to face up for the sole to lie flat on the ground
	const JPH::Vec3 box_up = (foot * info.BoxRotation) * JPH::Vec3::sAxisY();
	const JPH::Quat level = JPH::Quat::sFromTo(box_up, GravityFrame::FromPhysics().Up);

	return RelativeRotation(parent, (level * foot).Normalized());
}

void Ragdoll::ApplyMotor(uint32 body_index)
{
	const RDSegmentInfo& info = mSegments[body_index];

	if (!mbHasMotors || info.ParentBody < 0) {
		return;
	}

	const RDSegmentTarget& target = mSegmentTargets[body_index];
	JPH::SwingTwistConstraint* joint = GetJoint(body_index);

	const MotorOverride& motor_override = mMotorOverrides[body_index];
	const bool is_powered = !mbLimp && (info.Mode == eRagdollBodyMode::Powered) &&
							(motor_override.Mode != eMotorOverride::Off);

	if (!is_powered) {
		joint->SetSwingMotorState(JPH::EMotorState::Off);
		joint->SetTwistMotorState(JPH::EMotorState::Off);
		return;
	}

	// Muscles let go as the body falls, so it folds over the approach rather than snapping limp at the threshold
	const float32 fall = mbBalancing ? mBalance.GetStatus().FallProgress : 0.0f;
	const float32 strength = info.Strength * (1.0f - scFallLimp * fall);

	const float32 frequency = info.Muscle.Stiffness * motor_override.FrequencyScale *
							  (scMotorMinFrequencyScale + (1.0f - scMotorMinFrequencyScale) * strength);
	const float32 omega = FX_2PI * frequency;

	float32 inertia = motor_override.Inertia;

	if (inertia <= 0.0f && motor_override.bSupported) {
		inertia = GetSupportedInertia(body_index, mSegmentStates[body_index].Origin);
	}
	else if (inertia <= 0.0f) {
		inertia = GetSubtreeInertia(body_index, mSegmentStates[body_index].Origin);
	}

	const JPH::SpringSettings spring = MakeSpring(inertia, omega, info.Muscle.Damping);
	const float32 torque = info.Muscle.MaxTorque * strength;

	JPH::MotorSettings& swing = joint->GetSwingMotorSettings();
	JPH::MotorSettings& twist = joint->GetTwistMotorSettings();

	swing.mSpringSettings = spring;
	swing.SetTorqueLimit(torque);
	twist.mSpringSettings = spring;
	twist.SetTorqueLimit(torque);

	joint->SetSwingMotorState(JPH::EMotorState::Position);
	joint->SetTwistMotorState(JPH::EMotorState::Position);

	JPH::Quat joint_target = motor_override.Target;

	if (motor_override.Mode != eMotorOverride::Target) {
		const RDSegmentTarget& parent_target = mSegmentTargets[info.ParentBody];
		const JPH::Quat parent_inverse = parent_target.Orientation.Conjugated();
		const JPH::Vec3 relative_velocity = parent_inverse * (target.AngularVelocity - parent_target.AngularVelocity);
		const float32 lead = GetLeadTime(info.Muscle.Damping, omega);

		joint_target = IntegrateRotation(parent_inverse * target.Orientation, relative_velocity, lead);
	}

	if (motor_override.Mode == eMotorOverride::Target) {
		mOverrideTargets[body_index] = joint_target;
	}

	if (mbBalancing) {
		mHadOverride[body_index] = (motor_override.Mode == eMotorOverride::Target);
	}
	else if (mOverrideFade > 0.0f && mHadOverride[body_index]) {
		joint_target = joint_target.SLERP(mOverrideTargets[body_index], mOverrideFade);
	}

	const BodyLayer& layer = mLayers[body_index];

	if (layer.Weight > 0.0f) {
		joint_target = joint_target.SLERP(layer.Pose, layer.Weight);
	}

	JPH::Quat relative_target = (joint_target * layer.Offset).Normalized();

	if (info.bFoot) {
		relative_target = LevelFoot(body_index, relative_target);
	}

	joint->SetTargetOrientationBS(relative_target);
}

void Ragdoll::ApplyGravityCompensation(JPH::BodyInterface& body_interface, const JPH::Vec3 gravity)
{
	for (uint32 i = 1; i < mSegments.Size; i++) {
		const RDSegmentInfo& info = mSegments[i];
		const RDSegmentState& state = mSegmentStates[i];

		if (info.Mode != eRagdollBodyMode::Powered || mMotorOverrides[i].bNoGravityComp) {
			continue;
		}

		const float32 scale = MathUtil::Clamp(info.Strength / scGravityCompFullStrength, 0.0f, 1.0f) *
							  mLayers[i].GravityScale;

		if (scale <= 0.0f || state.SubtreeMass <= 0.0f) {
			continue;
		}

		const JPH::BodyID body_id = GetBodyID(i);
		const JPH::Vec3 gravity_torque = (state.SubtreeMoment - state.Origin * state.SubtreeMass).Cross(gravity) *
										 scale;

		body_interface.AddTorque(body_id, -gravity_torque);

		if (mSegments[info.ParentBody].Mode != eRagdollBodyMode::Kinematic) {
			body_interface.AddTorque(GetBodyID(info.ParentBody), gravity_torque);
		}
	}
}

void Ragdoll::ApplyRootAssist(JPH::BodyInterface& body_interface, const JPH::Vec3 gravity)
{
	const bool is_active = (mpRootAssist != nullptr) && (mRootAssist > 0.0f) && !mbBalancing &&
						   (mSegments[0].Mode == eRagdollBodyMode::Powered);

	if (mpRootAssist != nullptr) {
		mpRootAssist->SetEnabled(is_active);
	}

	if (!is_active) {
		return;
	}

	const RDSegmentTarget& target = mSegmentTargets[0];
	const RDSegmentState& root = mSegmentStates[0];

	const float32 total_mass = root.SubtreeMass;
	const JPH::Vec3 center_of_mass = root.SubtreeMoment / total_mass;
	const float32 inertia = GetSubtreeInertia(0, root.Center);

	const float32 omega = FX_2PI * scRootAssistFrequency;
	const float32 lead = GetLeadTime(scRootAssistDamping, omega);

	const JPH::SpringSettings linear_spring = MakeSpring(total_mass, omega, scRootAssistDamping, mRootAssist);
	const JPH::SpringSettings angular_spring = MakeSpring(inertia, omega, scRootAssistDamping, mRootAssist);

	for (uint32 axis = 0; axis < JPH::SixDOFConstraint::EAxis::Num; axis++) {
		const JPH::SixDOFConstraint::EAxis jolt_axis = static_cast<JPH::SixDOFConstraint::EAxis>(axis);
		JPH::MotorSettings& motor = mpRootAssist->GetMotorSettings(jolt_axis);

		if (axis < JPH::SixDOFConstraint::EAxis::RotationX) {
			motor.mSpringSettings = linear_spring;
			motor.SetForceLimit(mRootAssist * scRootAssistMaxForce);
		}
		else {
			motor.mSpringSettings = angular_spring;
			motor.SetTorqueLimit(mRootAssist * scRootAssistMaxTorque);
		}

		mpRootAssist->SetMotorState(jolt_axis, JPH::EMotorState::Position);
	}

	mpRootAssist->SetTargetPositionCS(target.Position + target.LinearVelocity * lead - mRootAnchor);
	mpRootAssist->SetTargetOrientationBS(IntegrateRotation(target.Orientation, target.AngularVelocity, lead));

	const JPH::BodyID root_id = GetBodyID(0);

	body_interface.AddForce(root_id, -gravity * (total_mass * mRootAssist));
	body_interface.AddTorque(root_id, -(center_of_mass - root.Center).Cross(gravity * (total_mass * mRootAssist)));
}

void Ragdoll::ClearMotorOverrides()
{
	for (MotorOverride& motor_override : mMotorOverrides) {
		motor_override = MotorOverride {};
	}
}

float32 Ragdoll::GetMotorTorqueFraction(uint32 body_index) const
{
	const RDSegmentInfo& info = mSegments[body_index];

	if (!mbHasMotors || info.ParentBody < 0) {
		return 0.0f;
	}

	const float32 limit = info.Muscle.MaxTorque * info.Strength;

	if (limit <= 0.0f) {
		return 0.0f;
	}

	const JPH::SwingTwistConstraint* joint = GetJoint(body_index);

	const JPH::Vec3 torque = joint->GetTotalLambdaMotor() / gPhysics->pBackend->GetCollisionStepTime();

	return torque.Length() / limit;
}

} // namespace fx::physics
