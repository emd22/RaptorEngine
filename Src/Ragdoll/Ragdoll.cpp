#include "Ragdoll.hpp"

#include "RagdollMath.hpp"

#include <ThirdParty/Jolt/Jolt.h>

#include <Engine.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <algorithm>
#include <cmath>

namespace fx::physics {

using namespace RagdollMath;

void Ragdoll::Activate(const Vec3f velocity)
{
	if (mpRagdoll == nullptr) {
		return;
	}

	mpRagdoll->SetLinearVelocity(JPH::Vec3(velocity.X, velocity.Y, velocity.Z));
	mpRagdoll->Activate();

	mbLimp = true;

	if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(true);
	}
}

void Ragdoll::Drive(const Mat4f& object_world_matrix, float32 delta_time, bool teleport)
{
	if (mpRagdoll == nullptr || !mpSkeleton.IsValid() || mbLimp) {
		return;
	}

	mWorldInverse = object_world_matrix.AffineInverse();

	const Skeleton& skel = *mpSkeleton;
	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	for (uint32 i = 0; i < mBodies.Size; i++) {
		BodyInfo& info = mBodies[i];
		BodyTarget& target = mTargets[i];

		const Mat4f bone_world = skel.WorldTransforms[info.BoneIndex] * object_world_matrix;
		const JPH::Quat orientation = RotationFromMatrix(bone_world).GetQuaternion().Normalized();
		const JPH::Vec3 position = RowAsVec3(bone_world, 3);

		if (teleport || !mbHasTargets || delta_time <= 0.0f) {
			target.LinearVelocity = JPH::Vec3::sZero();
			target.AngularVelocity = JPH::Vec3::sZero();
		}
		else {
			target.LinearVelocity = (position - target.Position) / delta_time;
			target.AngularVelocity = GetRotationVector(orientation * target.Orientation.Conjugated()) / delta_time;
		}

		target.Orientation = orientation;
		target.Position = position;

		if (info.Mode == eRagdollBodyMode::Kinematic) {
			if (teleport || info.bSnap) {
				SnapBodyToTarget(body_interface, i, JPH::EActivation::DontActivate);
				info.bSnap = false;
			}
		}
		else if (info.Mode == eRagdollBodyMode::Powered && teleport) {
			SnapBodyToTarget(body_interface, i, JPH::EActivation::Activate);
		}
	}

	mbHasTargets = true;
}

void Ragdoll::SnapBodyToTarget(JPH::BodyInterface& body_interface, uint32 body_index, JPH::EActivation activation)
{
	const BodyTarget& target = mTargets[body_index];
	const JPH::BodyID body_id = GetBodyID(body_index);

	body_interface.SetPositionAndRotation(body_id, target.Position, target.Orientation, activation);
	body_interface.SetLinearAndAngularVelocity(body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
}

void Ragdoll::GoLimp(const Mat4f& object_world_matrix)
{
	if (mpRagdoll == nullptr || mbLimp) {
		return;
	}

	mWorldInverse = object_world_matrix.AffineInverse();

	SetAllBodyModes(eRagdollBodyMode::Limp);
	SetAllBodyBlends(1.0f);

	mpRagdoll->Activate();

	mbLimp = true;
	mbPoseSettled = false;

	if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(true);
	}
}

void Ragdoll::SetBodyMode(uint32 body_index, eRagdollBodyMode mode)
{
	if (mpRagdoll == nullptr || body_index >= mBodies.Size || mBodies[body_index].Mode == mode) {
		return;
	}

	BodyInfo& info = mBodies[body_index];
	const eRagdollBodyMode previous_mode = info.Mode;
	info.Mode = mode;

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	const JPH::BodyID body_id = GetBodyID(body_index);

	if (mode == eRagdollBodyMode::Kinematic) {
		body_interface.SetMotionType(body_id, JPH::EMotionType::Kinematic, JPH::EActivation::DontActivate);
		info.bSnap = true;
	}
	else {
		body_interface.SetMotionType(body_id, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
		body_interface.SetGravityFactor(body_id, 1.0f);
		body_interface.ActivateBody(body_id);

		// Keep the animation's velocity (taken at the centre of mass) so the body does not stop dead
		if (previous_mode == eRagdollBodyMode::Kinematic) {
			const BodyTarget& target = mTargets[body_index];
			const JPH::Vec3 offset = JPH::Vec3(body_interface.GetCenterOfMassPosition(body_id) -
											   body_interface.GetPosition(body_id));

			body_interface.SetLinearAndAngularVelocity(
				body_id, target.LinearVelocity + target.AngularVelocity.Cross(offset), target.AngularVelocity);
		}
	}

	mbPoseSettled = false;
}

void Ragdoll::SetAllBodyModes(eRagdollBodyMode mode)
{
	for (uint32 i = 0; i < mBodies.Size; i++) {
		SetBodyMode(i, mode);
	}
}

void Ragdoll::SetBodyStrength(uint32 body_index, float32 strength)
{
	if (mpRagdoll == nullptr || body_index >= mBodies.Size) {
		return;
	}

	mBodies[body_index].Strength = MathUtil::Clamp(strength, 0.0f, 1.0f);
}

void Ragdoll::SetBodyBlend(uint32 body_index, float32 blend)
{
	if (body_index < mBodies.Size) {
		mBodies[body_index].Blend = MathUtil::Clamp(blend, 0.0f, 1.0f);
	}
}

void Ragdoll::SetAllBodyBlends(float32 blend)
{
	for (uint32 i = 0; i < mBodies.Size; i++) {
		SetBodyBlend(i, blend);
	}
}

void Ragdoll::SetRootAssist(float32 assist) { mRootAssist = MathUtil::Clamp(assist, 0.0f, 1.0f); }

void Ragdoll::SetBalancing(bool balancing)
{
	if (mpRagdoll == nullptr || !mBalance.IsValid()) {
		balancing = false;
	}

	if (balancing == mbBalancing) {
		return;
	}

	mbBalancing = balancing;

	if (balancing) {
		mBalance.Reset();
	}
	else {
		mBalance.Clear(*this);
	}
}

void Ragdoll::RebaseTargets() { mbHasTargets = false; }

void Ragdoll::ClearBodyLayers()
{
	for (BodyLayer& layer : mLayers) {
		layer = BodyLayer {};
	}
}

void Ragdoll::SetBodyLayers(const SizedArray<BodyLayer>& layers, float32 gravity_scale)
{
	for (uint32 i = 0; i < mLayers.Size && i < layers.Size; i++) {
		mLayers[i] = layers[i];
		mLayers[i].GravityScale = gravity_scale;
	}
}

void Ragdoll::AddImpulse(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point)
{
	const int32 body_index = FindBodyIndex(body_id);

	if (body_index < 0 || mBodies[body_index].Mode == eRagdollBodyMode::Kinematic) {
		return;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	body_interface.ActivateBody(body_id);
	body_interface.AddImpulse(body_id, JPH::Vec3(impulse.X, impulse.Y, impulse.Z),
							  JPH::RVec3(point.X, point.Y, point.Z));

	mbPoseSettled = false;
}

int32 Ragdoll::FindBodyIndex(JPH::BodyID body_id) const
{
	if (mpRagdoll == nullptr) {
		return -1;
	}

	for (uint32 i = 0; i < mBodies.Size; i++) {
		if (GetBodyID(i) == body_id) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

bool Ragdoll::FindBoneForBody(JPH::BodyID body_id, uint32& out_bone_index) const
{
	const int32 body_index = FindBodyIndex(body_id);

	if (body_index < 0) {
		return false;
	}

	out_bone_index = mBodies[body_index].BoneIndex;
	return true;
}

float32 Ragdoll::GetBodyMass(uint32 body_index) const
{
	if (body_index >= mBodies.Size) {
		return 0.0f;
	}

	return mBodies[body_index].Mass;
}

eRagdollBodyPart Ragdoll::GetBodyPart(uint32 body_index) const
{
	if (body_index >= mBodies.Size) {
		return eRagdollBodyPart::Torso;
	}

	return mBodies[body_index].Part;
}

bool Ragdoll::GetBodyTrackingError(uint32 body_index, float32& out_distance, float32& out_angle) const
{
	if (mpRagdoll == nullptr || body_index >= mBodies.Size) {
		return false;
	}

	JPH::RVec3 position;
	JPH::Quat orientation;
	gPhysics->pBackend->GetBodyInterface().GetPositionAndRotation(GetBodyID(body_index), position, orientation);

	const BodyTarget& target = mTargets[body_index];
	const float32 alignment = std::min(std::abs(orientation.Dot(target.Orientation)), 1.0f);

	out_distance = (JPH::Vec3(position) - target.Position).Length();
	out_angle = 2.0f * std::acos(alignment);

	return true;
}

float32 Ragdoll::GetWorstJointError() const
{
	if (mpRagdoll == nullptr) {
		return 0.0f;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	float32 worst = 0.0f;

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const BodyInfo& info = mBodies[i];

		if (info.ParentBody < 0) {
			continue;
		}

		JPH::RVec3 parent_position;
		JPH::Quat parent_orientation;
		body_interface.GetPositionAndRotation(GetBodyID(info.ParentBody), parent_position, parent_orientation);

		const JPH::Vec3 anchor = JPH::Vec3(parent_position) + parent_orientation * info.ParentAnchor;

		worst = std::max(worst, (JPH::Vec3(body_interface.GetPosition(GetBodyID(i))) - anchor).Length());
	}

	return worst;
}

Vec3f Ragdoll::GetBodyOrigin(uint32 body_index) const
{
	const JPH::RVec3 position = gPhysics->pBackend->GetBodyInterface().GetPosition(GetBodyID(body_index));
	return Vec3f(position.GetX(), position.GetY(), position.GetZ());
}

Vec3f Ragdoll::GetBodyTargetOrigin(uint32 body_index) const
{
	if (body_index >= mTargets.Size) {
		return Vec3f::sZero;
	}

	return Vec3f(mTargets[body_index].Position);
}

Vec3f Ragdoll::GetBodyPosition(uint32 body_index) const
{
	const JPH::RVec3 position = gPhysics->pBackend->GetBodyInterface().GetCenterOfMassPosition(GetBodyID(body_index));
	return Vec3f(position.GetX(), position.GetY(), position.GetZ());
}

void Ragdoll::SampleBody(JPH::BodyInterface& body_interface, uint32 body_index, BodyState& out_state) const
{
	const JPH::BodyID body_id = GetBodyID(body_index);

	JPH::RVec3 position;
	body_interface.GetPositionAndRotation(body_id, position, out_state.Orientation);

	out_state.Origin = JPH::Vec3(position);
	out_state.Center = JPH::Vec3(body_interface.GetCenterOfMassPosition(body_id));
	out_state.Velocity = body_interface.GetLinearVelocity(body_id);
}

bool Ragdoll::GetMassAverage(const SizedArray<BodyState>& states, JPH::Vec3& out_center, JPH::Vec3& out_velocity) const
{
	if (mTotalMass <= 0.0f) {
		return false;
	}

	JPH::Vec3 moment = JPH::Vec3::sZero();
	JPH::Vec3 momentum = JPH::Vec3::sZero();

	for (uint32 i = 0; i < mBodies.Size; i++) {
		moment += states[i].Center * mBodies[i].Mass;
		momentum += states[i].Velocity * mBodies[i].Mass;
	}

	out_center = moment / mTotalMass;
	out_velocity = momentum / mTotalMass;

	return true;
}

bool Ragdoll::GetBodyGroundContact(uint32 body_index, BodyGroundContact& out_contact) const
{
	if (mpRagdoll == nullptr || body_index >= mBodies.Size) {
		out_contact = BodyGroundContact {};
		return false;
	}

	gPhysics->pBackend->GetBodyGroundContact(GetBodyID(body_index), out_contact);

	return out_contact.Count > 0;
}

bool Ragdoll::IsSupportNormal(const BodyGroundContact& contact, const JPH::Vec3 up, float32 min_up)
{
	return Vec3f::ToJolt(contact.Normal).Dot(up) >= min_up;
}

bool Ragdoll::IsBodyOnGround(uint32 body_index, const JPH::Vec3 up, float32 min_up) const
{
	BodyGroundContact contact;

	return GetBodyGroundContact(body_index, contact) && IsSupportNormal(contact, up, min_up);
}

Mat4f Ragdoll::GetBodyModelMatrix(uint32 body_index) const
{
	JPH::RVec3 position;
	JPH::Quat orientation;
	gPhysics->pBackend->GetBodyInterface().GetPositionAndRotation(GetBodyID(body_index), position, orientation);

	const Mat4f world = MatrixFromRotationTranslation(JPH::Mat44::sRotation(orientation), JPH::Vec3(position));

	Mat4f model = world * mWorldInverse;
	NormalizeRotationRows(model);
	ScaleRotationRows(model, mBodies[body_index].BoneScale);

	return model;
}

void Ragdoll::WriteToSkeleton()
{
	if (mpRagdoll == nullptr || !mpSkeleton.IsValid()) {
		return;
	}

	if (mbLimp) {
		const bool is_asleep = !mpRagdoll->IsActive();

		if (is_asleep && mbPoseSettled) {
			return;
		}

		mbPoseSettled = is_asleep;

		for (uint32 i = 0; i < mBodies.Size; i++) {
			mDrivenWorld[mBodies[i].BoneIndex] = GetBodyModelMatrix(i);
			mIsDriven[mBodies[i].BoneIndex] = 1;
		}

		mpSkeleton->PoseFromDrivenBones(mDrivenWorld.pData, mIsDriven.pData);
		return;
	}

	const Skeleton& skel = *mpSkeleton;
	bool any_driven = false;

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const BodyInfo& info = mBodies[i];
		const bool is_driven = (info.Mode != eRagdollBodyMode::Kinematic) && (info.Blend > 0.0f);

		mIsDriven[info.BoneIndex] = static_cast<uint8>(is_driven);

		if (!is_driven) {
			continue;
		}

		any_driven = true;

		Mat4f model = GetBodyModelMatrix(i);

		if (info.Blend < 1.0f) {
			const Mat4f& animated = skel.WorldTransforms[info.BoneIndex];

			const JPH::Quat from = RotationFromMatrix(animated).GetQuaternion().Normalized();
			const JPH::Quat to = RotationFromMatrix(model).GetQuaternion().Normalized();
			const JPH::Vec3 from_position = RowAsVec3(animated, 3);
			const JPH::Vec3 to_position = RowAsVec3(model, 3);

			model = MatrixFromRotationTranslation(JPH::Mat44::sRotation(from.SLERP(to, info.Blend).Normalized()),
												  from_position + (to_position - from_position) * info.Blend);
			ScaleRotationRows(model, info.BoneScale);
		}

		mDrivenWorld[info.BoneIndex] = model;
	}

	if (any_driven) {
		mpSkeleton->PoseFromDrivenBones(mDrivenWorld.pData, mIsDriven.pData);
	}
}

void Ragdoll::Destroy()
{
	if (mpPhysicsSystem != nullptr && mpRootAssist != nullptr) {
		mpPhysicsSystem->RemoveConstraint(mpRootAssist);
	}

	mpRootAssist = nullptr;

	if (mpPhysicsSystem != nullptr) {
		mpPhysicsSystem->RemoveStepListener(this);
		mpPhysicsSystem = nullptr;
	}

	if (mpRagdoll != nullptr) {
		mpRagdoll->RemoveFromPhysicsSystem();
		mpRagdoll = nullptr;
	}

	if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(false);
	}

	mpSkeleton = nullptr;
	mBodies.Clear();
	mTargets.Free();
	mStates.Free();
	mMotorOverrides.Free();
	mLayers.Free();
	mTotalMass = 0.0f;
	mbHasTargets = false;
	mbHasMotors = false;
	mbLimp = false;
	mRig = RagdollRig {};
	mBalance = RagdollBalance {};
	mbBalancing = false;
	mDrivenWorld.Free();
	mIsDriven.Free();
	mbPoseSettled = false;
}

} // namespace fx::physics
