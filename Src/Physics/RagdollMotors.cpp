#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"
#include "Ragdoll.hpp"
#include "RagdollMath.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <algorithm>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scMotorMinFrequencyScale = 0.2f;
constexpr float32 scMinSubtreeInertia = 1e-4f;
constexpr float32 scGravityCompFullStrength = 0.25f;

constexpr float32 scRootAssistFrequency = 6.0f;
constexpr float32 scRootAssistDamping = 1.0f;
constexpr float32 scRootAssistMaxForce = 1500.0f;
constexpr float32 scRootAssistMaxTorque = 600.0f;

} // namespace

void Ragdoll::OnStep(const JPH::PhysicsStepListenerContext& inContext)
{
	if (mpRagdoll == nullptr) {
		return;
	}

	JPH::BodyInterface& body_interface = inContext.mPhysicsSystem->GetBodyInterfaceNoLock();

	if (inContext.mIsFirstStep) {
		mSubstep = 0;
	}

	const uint32 steps_left = std::max(static_cast<uint32>(JoltPhysicsBackend::scCollisionSteps) - mSubstep, 1u);
	const float32 fraction = inContext.mIsLastStep ? 1.0f : 1.0f / static_cast<float32>(steps_left);

	mSubstep++;

	MoveKinematicBodies(body_interface, fraction, inContext.mDeltaTime);

	const JPH::Vec3 gravity = inContext.mPhysicsSystem->GetGravity();

	const uint32 contact_generation = gPhysics->pBackend->GetGroundContactGeneration();
	const bool contacts_fresh = (contact_generation == mContactGeneration);
	mContactGeneration = contact_generation;

	if (!mbLimp) {
		UpdateSubtrees(body_interface);

		if (mbBalancing) {
			mBalance.Step(*this, body_interface, inContext.mDeltaTime, gravity, contacts_fresh);
		}
	}

	for (uint32 i = 0; i < mBodies.Size; i++) {
		ApplyMotor(i);
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
	for (uint32 i = 0; i < mBodies.Size; i++) {
		if (mBodies[i].Mode != eRagdollBodyMode::Kinematic) {
			continue;
		}

		const BodyTarget& target = mTargets[i];
		const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(i));

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
	for (uint32 i = 0; i < mBodies.Size; i++) {
		const BodyInfo& info = mBodies[i];
		const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(i));
		BodyState& state = mStates[i];

		state.Origin = JPH::Vec3(body_interface.GetPosition(body_id));
		state.Center = JPH::Vec3(body_interface.GetCenterOfMassPosition(body_id));

		const float32 mass = (info.Mode != eRagdollBodyMode::Kinematic) ? info.Mass : 0.0f;

		state.SubtreeMass = mass;
		state.SubtreeMoment = state.Center * mass;
		state.SubtreeSecondMoment = mass * state.Center.LengthSq() + ((mass > 0.0f) ? info.Inertia : 0.0f);
	}

	for (uint32 i = mBodies.Size - 1; i > 0; i--) {
		const BodyState& child = mStates[i];
		BodyState& parent = mStates[mBodies[i].ParentBody];

		parent.SubtreeMass += child.SubtreeMass;
		parent.SubtreeMoment += child.SubtreeMoment;
		parent.SubtreeSecondMoment += child.SubtreeSecondMoment;
	}
}

float32 Ragdoll::GetRawSubtreeInertia(uint32 body_index, const JPH::Vec3 pivot) const
{
	const BodyState& state = mStates[body_index];

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

void Ragdoll::ApplyMotor(uint32 body_index)
{
	const BodyInfo& info = mBodies[body_index];

	if (!mbHasMotors || info.ParentBody < 0) {
		return;
	}

	const BodyTarget& target = mTargets[body_index];

	JPH::SwingTwistConstraint* joint = static_cast<JPH::SwingTwistConstraint*>(
		mpRagdoll->GetConstraint(static_cast<int>(body_index) - 1));

	const MotorOverride& motor_override = mMotorOverrides[body_index];
	const bool is_powered = !mbLimp && (info.Mode == eRagdollBodyMode::Powered) &&
							(motor_override.Mode != eMotorOverride::Off);

	if (!is_powered) {
		joint->SetSwingMotorState(JPH::EMotorState::Off);
		joint->SetTwistMotorState(JPH::EMotorState::Off);
		return;
	}

	const float32 frequency = info.Muscle.Stiffness * motor_override.FrequencyScale *
							  (scMotorMinFrequencyScale + (1.0f - scMotorMinFrequencyScale) * info.Strength);
	const float32 omega = FX_2PI * frequency;

	float32 inertia = motor_override.Inertia;

	if (inertia <= 0.0f && motor_override.bSupported) {
		inertia = GetSupportedInertia(body_index, mStates[body_index].Origin);
	}
	else if (inertia <= 0.0f) {
		inertia = GetSubtreeInertia(body_index, mStates[body_index].Origin);
	}

	const JPH::SpringSettings spring(JPH::ESpringMode::StiffnessAndDamping, inertia * omega * omega,
									 2.0f * info.Muscle.Damping * omega * inertia);
	const float32 torque = info.Muscle.MaxTorque * info.Strength;

	JPH::MotorSettings& swing = joint->GetSwingMotorSettings();
	JPH::MotorSettings& twist = joint->GetTwistMotorSettings();

	swing.mSpringSettings = spring;
	swing.SetTorqueLimit(torque);
	twist.mSpringSettings = spring;
	twist.SetTorqueLimit(torque);

	joint->SetSwingMotorState(JPH::EMotorState::Position);
	joint->SetTwistMotorState(JPH::EMotorState::Position);

	if (motor_override.Mode == eMotorOverride::Target) {
		joint->SetTargetOrientationBS(motor_override.Target);
		return;
	}

	const BodyTarget& parent_target = mTargets[info.ParentBody];
	const JPH::Quat parent_inverse = parent_target.Orientation.Conjugated();
	const JPH::Vec3 relative_velocity = parent_inverse * (target.AngularVelocity - parent_target.AngularVelocity);
	const float32 lead = 2.0f * info.Muscle.Damping / omega;

	joint->SetTargetOrientationBS(IntegrateRotation(parent_inverse * target.Orientation, relative_velocity, lead));
}

void Ragdoll::ApplyGravityCompensation(JPH::BodyInterface& body_interface, const JPH::Vec3 gravity)
{
	for (uint32 i = 1; i < mBodies.Size; i++) {
		const BodyInfo& info = mBodies[i];
		const BodyState& state = mStates[i];

		if (info.Mode != eRagdollBodyMode::Powered || mMotorOverrides[i].bNoGravityComp) {
			continue;
		}

		const float32 scale = std::clamp(info.Strength / scGravityCompFullStrength, 0.0f, 1.0f);

		if (scale <= 0.0f || state.SubtreeMass <= 0.0f) {
			continue;
		}

		const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(i));
		const JPH::Vec3 gravity_torque = (state.SubtreeMoment - state.Origin * state.SubtreeMass).Cross(gravity) *
										 scale;

		body_interface.AddTorque(body_id, -gravity_torque);

		if (mBodies[info.ParentBody].Mode != eRagdollBodyMode::Kinematic) {
			body_interface.AddTorque(mpRagdoll->GetBodyID(info.ParentBody), gravity_torque);
		}
	}
}

void Ragdoll::ApplyRootAssist(JPH::BodyInterface& body_interface, const JPH::Vec3 gravity)
{
	const bool is_active = (mpRootAssist != nullptr) && (mRootAssist > 0.0f) && !mbBalancing &&
						   (mBodies[0].Mode == eRagdollBodyMode::Powered);

	if (mpRootAssist != nullptr) {
		mpRootAssist->SetEnabled(is_active);
	}

	if (!is_active) {
		return;
	}

	const BodyTarget& target = mTargets[0];
	const BodyState& root = mStates[0];

	const float32 total_mass = root.SubtreeMass;
	const JPH::Vec3 center_of_mass = root.SubtreeMoment / total_mass;
	const float32 inertia = GetSubtreeInertia(0, root.Center);

	const float32 omega = FX_2PI * scRootAssistFrequency;
	const float32 lead = 2.0f * scRootAssistDamping / omega;

	const JPH::SpringSettings linear_spring(JPH::ESpringMode::StiffnessAndDamping,
											mRootAssist * total_mass * omega * omega,
											mRootAssist * 2.0f * scRootAssistDamping * omega * total_mass);
	const JPH::SpringSettings angular_spring(JPH::ESpringMode::StiffnessAndDamping,
											 mRootAssist * inertia * omega * omega,
											 mRootAssist * 2.0f * scRootAssistDamping * omega * inertia);

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

	const JPH::BodyID root_id = mpRagdoll->GetBodyID(0);

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
	const BodyInfo& info = mBodies[body_index];

	if (!mbHasMotors || info.ParentBody < 0) {
		return 0.0f;
	}

	const float32 limit = info.Muscle.MaxTorque * info.Strength;

	if (limit <= 0.0f) {
		return 0.0f;
	}

	const JPH::SwingTwistConstraint* joint = static_cast<const JPH::SwingTwistConstraint*>(
		mpRagdoll->GetConstraint(static_cast<int>(body_index) - 1));

	const JPH::Vec3 torque = joint->GetTotalLambdaMotor() / gPhysics->pBackend->GetCollisionStepTime();

	return torque.Length() / limit;
}

} // namespace fx::physics
