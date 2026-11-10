#include "Ragdoll.hpp"

#include "RagdollMath.hpp"

#include <ThirdParty/Jolt/Jolt.h>

#include <Engine.hpp>
#include <Math/TwoBoneIK.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scProbeUp = 0.3f;
constexpr float32 scProbeDown = 1.0f;

} // namespace

void Ragdoll::Activate(const FVec3 velocity)
{
	if (mpRagdoll == nullptr) {
		return;
	}

	mpRagdoll->SetLinearVelocity(FVec3::ToJolt(velocity));
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

	for (uint32 i = 0; i < mSegments.Size; i++) {
		RDSegmentInfo& info = mSegments[i];
		RDSegmentTarget& target = mSegmentTargets[i];

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
				SnapSegmentToTarget(body_interface, i, JPH::EActivation::DontActivate);
				info.bSnap = false;
			}
		}
		else if (info.Mode == eRagdollBodyMode::Powered && teleport) {
			SnapSegmentToTarget(body_interface, i, JPH::EActivation::Activate);
		}
	}

	mbHasTargets = true;
}

void Ragdoll::SnapSegmentToTarget(JPH::BodyInterface& body_interface, uint32 body_index, JPH::EActivation activation)
{
	const RDSegmentTarget& target = mSegmentTargets[body_index];
	const JPH::BodyID body_id = GetBodyID(body_index);

	body_interface.SetPositionAndRotation(body_id, target.Position, target.Orientation, activation);
	body_interface.SetLinearAndAngularVelocity(body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());

	mPreviousPoses[body_index].bValid = false;
}

void Ragdoll::StorePreviousPoses(JPH::BodyInterface& body_interface)
{
	for (uint32 i = 0; i < mSegments.Size; i++) {
		BodyPose& pose = mPreviousPoses[i];

		JPH::RVec3 position;
		body_interface.GetPositionAndRotation(GetBodyID(i), position, pose.Orientation);

		pose.Position = JPH::Vec3(position);
		pose.bValid = true;
	}
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

JPH::ObjectLayer Ragdoll::GetModeLayer(eRagdollBodyMode mode)
{
	switch (mode) {
	case eRagdollBodyMode::Kinematic:
		return PhLayer::RagdollKinematic;
	case eRagdollBodyMode::Powered:
		return PhLayer::RagdollPowered;
	case eRagdollBodyMode::Limp:
		return PhLayer::RagdollLimp;
	}

	return PhLayer::RagdollLimp;
}

void Ragdoll::SetBodyMode(uint32 body_index, eRagdollBodyMode mode)
{
	if (mpRagdoll == nullptr || body_index >= mSegments.Size || mSegments[body_index].Mode == mode) {
		return;
	}

	RDSegmentInfo& info = mSegments[body_index];
	const eRagdollBodyMode previous_mode = info.Mode;
	info.Mode = mode;

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	const JPH::BodyID body_id = GetBodyID(body_index);

	body_interface.SetObjectLayer(body_id, GetModeLayer(mode));

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
			const RDSegmentTarget& target = mSegmentTargets[body_index];
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
	bool was_kinematic = false;

	// A hit landing while the skeleton is still easing back to its animation picks up from the pose on screen
	if (mode != eRagdollBodyMode::Kinematic && mPoseBlend > 0.0f) {
		MoveBodiesToPoseBlend();
	}

	for (uint32 i = 0; i < mSegments.Size; i++) {
		if (mSegments[i].Mode == eRagdollBodyMode::Kinematic) {
			was_kinematic = true;
		}

		SetBodyMode(i, mode);
	}

	if (was_kinematic && mode != eRagdollBodyMode::Kinematic) {
		PlaceFeetOnGround();
	}
}

void Ragdoll::PlaceFeetOnGround()
{
	// How far above the ground a sole still counts as planted (m)
	constexpr float32 scPlantedHeight = 0.02f;

	if (mpRagdoll == nullptr || !mRig.bCanBalance) {
		return;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	const JPH::Vec3 up = JPH::Vec3::sAxisY();

	for (const RagdollRigLeg& leg : mRig.Legs) {
		const JPH::BodyID thigh_id = GetBodyID(leg.Thigh);
		const JPH::BodyID shin_id = GetBodyID(leg.Shin);
		const JPH::BodyID foot_id = GetBodyID(leg.Foot);

		const JPH::Vec3 hip = JPH::Vec3(body_interface.GetPosition(thigh_id));
		const JPH::Vec3 knee = JPH::Vec3(body_interface.GetPosition(shin_id));
		const JPH::Vec3 ankle = JPH::Vec3(body_interface.GetPosition(foot_id));

		const RayResult ground = gPhysics->pBackend->RaycastStatic(
			FVec3(ankle.GetX(), ankle.GetY() + scProbeUp, ankle.GetZ()), FVec3(0.0f, -(scProbeUp + scProbeDown), 0.0f));

		if (!ground.bHit) {
			continue;
		}

		const auto get_clearance = [&](const JPH::Vec3 position, const JPH::Quat& rotation)
		{
			float32 lowest = std::numeric_limits<float32>::max();

			for (const JPH::Vec3& corner : leg.SoleCorners) {
				lowest = std::min(lowest, (position + rotation * corner).Dot(up));
			}

			return lowest - ground.Point.Y;
		};

		JPH::Quat foot_rotation = body_interface.GetRotation(foot_id);
		const float32 clearance = get_clearance(ankle, foot_rotation);

		// A foot in mid air keeps the velocity the animation gave it
		if (clearance > scPlantedHeight) {
			continue;
		}

		JPH::Vec3 new_knee = knee;
		JPH::Vec3 new_ankle = ankle;

		// Clips rarely ground a rigid sole to the centimetre, so a planted foot can start under the floor. Left there,
		// the depenetration kicks the body the moment it turns dynamic. The whole leg moves to fit, since moving just
		// the foot would tear the ankle apart and let the joint snap back
		if (clearance < 0.0f) {
			const JPH::Vec3 sole_up = foot_rotation * (mSegments[leg.Foot].RestOrientation.Conjugated() * up);

			foot_rotation = (JPH::Quat::sFromTo(sole_up, up) * foot_rotation).Normalized();

			const JPH::Vec3 lifted = ankle - up * std::min(get_clearance(ankle, foot_rotation), 0.0f);
			const TwoBoneIKResult ik = SolveTwoBoneIK(FVec3::FromJolt(hip), FVec3::FromJolt(lifted), leg.Upper,
													  leg.Lower, FVec3::FromJolt(knee - hip));

			new_knee = FVec3::ToJolt(ik.Mid);
			new_ankle = FVec3::ToJolt(ik.End);

			const JPH::Quat thigh_turn = JPH::Quat::sFromTo((knee - hip).Normalized(), (new_knee - hip).Normalized());
			const JPH::Quat shin_turn = JPH::Quat::sFromTo((ankle - knee).Normalized(),
														   (new_ankle - new_knee).Normalized());

			body_interface.SetRotation(thigh_id, (thigh_turn * body_interface.GetRotation(thigh_id)).Normalized(),
									   JPH::EActivation::Activate);
			body_interface.SetPositionAndRotation(shin_id, JPH::RVec3(new_knee),
												  (shin_turn * body_interface.GetRotation(shin_id)).Normalized(),
												  JPH::EActivation::Activate);
			body_interface.SetPositionAndRotation(foot_id, JPH::RVec3(new_ankle), foot_rotation,
												  JPH::EActivation::Activate);

			mPreviousPoses[leg.Thigh].bValid = false;
			mPreviousPoses[leg.Shin].bValid = false;
			mPreviousPoses[leg.Foot].bValid = false;
		}

		// A foot standing on the floor isn't moving, whatever speed an in-place clip has it sliding at. The leg above
		// it pivots about that foot, from standing still at the ankle to the pelvis' own speed at the hip, so no joint
		// starts out being pulled apart
		const JPH::Vec3 hip_velocity = body_interface.GetPointVelocity(GetBodyID(0), JPH::RVec3(hip));
		const float32 span = std::max((hip - new_ankle).Length(), 1e-3f);
		const JPH::Vec3 knee_velocity = hip_velocity *
										MathUtil::Clamp((new_knee - new_ankle).Length() / span, 0.0f, 1.0f);

		const auto set_link_velocity = [&](const JPH::BodyID body, const JPH::Vec3 start,
										   const JPH::Vec3 start_velocity, const JPH::Vec3 end,
										   const JPH::Vec3 end_velocity)
		{
			const JPH::Vec3 link = end - start;
			const JPH::Vec3 angular = link.Cross(end_velocity - start_velocity) / std::max(link.LengthSq(), 1e-4f);
			const JPH::Vec3 center = JPH::Vec3(body_interface.GetCenterOfMassPosition(body));

			body_interface.SetLinearAndAngularVelocity(body, start_velocity + angular.Cross(center - start), angular);
		};

		set_link_velocity(thigh_id, hip, hip_velocity, new_knee, knee_velocity);
		set_link_velocity(shin_id, new_knee, knee_velocity, new_ankle, JPH::Vec3::sZero());
		body_interface.SetLinearAndAngularVelocity(foot_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
	}

	mbPoseSettled = false;
}

float32 Ragdoll::GetSoleClearance(uint32 leg_index) const
{
	const RagdollRigLeg& leg = mRig.Legs[leg_index];
	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	JPH::RVec3 position;
	JPH::Quat rotation;
	body_interface.GetPositionAndRotation(GetBodyID(leg.Foot), position, rotation);

	const JPH::Vec3 ankle = JPH::Vec3(position);
	const RayResult ground = gPhysics->pBackend->RaycastStatic(
		FVec3(ankle.GetX(), ankle.GetY() + scProbeUp, ankle.GetZ()), FVec3(0.0f, -(scProbeUp + scProbeDown), 0.0f));

	if (!ground.bHit) {
		return std::numeric_limits<float32>::max();
	}

	float32 lowest = std::numeric_limits<float32>::max();

	for (const JPH::Vec3& corner : leg.SoleCorners) {
		lowest = std::min(lowest, (ankle + rotation * corner).GetY());
	}

	return lowest - ground.Point.Y;
}

void Ragdoll::SetBodyStrength(uint32 body_index, float32 strength)
{
	if (mpRagdoll == nullptr || body_index >= mSegments.Size) {
		return;
	}

	mSegments[body_index].Strength = MathUtil::Clamp(strength, 0.0f, 1.0f);
}

void Ragdoll::SetBodyBlend(uint32 body_index, float32 blend)
{
	if (body_index < mSegments.Size) {
		mSegments[body_index].Blend = MathUtil::Clamp(blend, 0.0f, 1.0f);
	}
}

void Ragdoll::SetAllBodyBlends(float32 blend)
{
	for (uint32 i = 0; i < mSegments.Size; i++) {
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
		mOverrideFade = 0.0f;
	}
	else {
		mBalance.Clear(*this);
		mOverrideFade = 1.0f;
	}
}

void Ragdoll::RebaseTargets() { mbHasTargets = false; }

void Ragdoll::BeginPoseBlend()
{
	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	for (uint32 i = 0; i < mSegments.Size; i++) {
		JPH::RVec3 position;
		body_interface.GetPositionAndRotation(GetBodyID(i), position, mPoseBlendFrom[i].Orientation);

		mPoseBlendFrom[i].Position = JPH::Vec3(position);
		mPoseBlendFrom[i].bValid = true;
	}

	mPoseBlend = 1.0f;
}

void Ragdoll::GetPoseBlendWorld(uint32 body_index, JPH::Vec3& out_position, JPH::Quat& out_orientation) const
{
	const BodyPose& from = mPoseBlendFrom[body_index];
	const RDSegmentTarget& target = mSegmentTargets[body_index];

	out_position = target.Position + (from.Position - target.Position) * mPoseBlend;
	out_orientation = target.Orientation.SLERP(from.Orientation, mPoseBlend).Normalized();
}

void Ragdoll::MoveBodiesToPoseBlend()
{
	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	for (uint32 i = 0; i < mSegments.Size; i++) {
		JPH::Vec3 position;
		JPH::Quat orientation;
		GetPoseBlendWorld(i, position, orientation);

		body_interface.SetPositionAndRotation(GetBodyID(i), JPH::RVec3(position), orientation,
											  JPH::EActivation::Activate);
		mPreviousPoses[i].bValid = false;
	}

	mPoseBlend = 0.0f;
}

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

void Ragdoll::AddImpulse(JPH::BodyID body_id, const FVec3 impulse, const FVec3 point)
{
	const int32 body_index = FindSegmentIndex(body_id);

	if (body_index < 0 || mSegments[body_index].Mode == eRagdollBodyMode::Kinematic) {
		return;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	body_interface.ActivateBody(body_id);
	body_interface.AddImpulse(body_id, FVec3::ToJolt(impulse), FVec3::ToJolt(point));

	mbPoseSettled = false;
}

int32 Ragdoll::FindSegmentIndex(JPH::BodyID body_id) const
{
	if (mpRagdoll == nullptr) {
		return -1;
	}

	for (uint32 i = 0; i < mSegments.Size; i++) {
		if (GetBodyID(i) == body_id) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

bool Ragdoll::FindBoneForSegment(JPH::BodyID body_id, uint32& out_bone_index) const
{
	const int32 body_index = FindSegmentIndex(body_id);

	if (body_index < 0) {
		return false;
	}

	out_bone_index = mSegments[body_index].BoneIndex;
	return true;
}

float32 Ragdoll::GetBodyMass(uint32 body_index) const
{
	if (body_index >= mSegments.Size) {
		return 0.0f;
	}

	return mSegments[body_index].Mass;
}

eRDBodyPart Ragdoll::GetBodyPart(uint32 body_index) const
{
	if (body_index >= mSegments.Size) {
		return eRDBodyPart::Torso;
	}

	return mSegments[body_index].Part;
}

bool Ragdoll::GetBodyTrackingError(uint32 body_index, float32& out_distance, float32& out_angle) const
{
	if (mpRagdoll == nullptr || body_index >= mSegments.Size) {
		return false;
	}

	JPH::RVec3 position;
	JPH::Quat orientation;
	gPhysics->pBackend->GetBodyInterface().GetPositionAndRotation(GetBodyID(body_index), position, orientation);

	const RDSegmentTarget& target = mSegmentTargets[body_index];
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

	for (uint32 i = 0; i < mSegments.Size; i++) {
		const RDSegmentInfo& info = mSegments[i];

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

FVec3 Ragdoll::GetBodyOrigin(uint32 body_index) const
{
	const JPH::RVec3 position = gPhysics->pBackend->GetBodyInterface().GetPosition(GetBodyID(body_index));
	return FVec3(position.GetX(), position.GetY(), position.GetZ());
}

FVec3 Ragdoll::GetPelvisHeading(bool animated) const
{
	if (mSegments.Size == 0) {
		return FVec3::sZero;
	}

	JPH::Quat orientation = gPhysics->pBackend->GetBodyInterface().GetRotation(GetBodyID(0));

	if (animated) {
		orientation = mSegmentTargets[0].Orientation;
	}

	JPH::Vec3 forward = (orientation * mSegments[0].RestOrientation.Conjugated()) * mRig.RestForward;
	forward.SetY(0.0f);

	return FVec3(forward.NormalizedOr(JPH::Vec3::sZero()));
}

FVec3 Ragdoll::GetBodyTargetOrigin(uint32 body_index) const
{
	if (body_index >= mSegmentTargets.Size) {
		return FVec3::sZero;
	}

	return FVec3(mSegmentTargets[body_index].Position);
}

FVec3 Ragdoll::GetBodyPosition(uint32 body_index) const
{
	const JPH::RVec3 position = gPhysics->pBackend->GetBodyInterface().GetCenterOfMassPosition(GetBodyID(body_index));
	return FVec3(position.GetX(), position.GetY(), position.GetZ());
}

void Ragdoll::SampleSegment(JPH::BodyInterface& body_interface, uint32 body_index, RDSegmentState& out_state) const
{
	const JPH::BodyID body_id = GetBodyID(body_index);

	JPH::RVec3 position;
	body_interface.GetPositionAndRotation(body_id, position, out_state.Orientation);

	out_state.Origin = JPH::Vec3(position);
	out_state.Center = JPH::Vec3(body_interface.GetCenterOfMassPosition(body_id));
	out_state.Velocity = body_interface.GetLinearVelocity(body_id);
	out_state.AngularVelocity = body_interface.GetAngularVelocity(body_id);
}

bool Ragdoll::GetMassAverage(const SizedArray<RDSegmentState>& states, JPH::Vec3& out_center,
							 JPH::Vec3& out_velocity) const
{
	if (mTotalMass <= 0.0f) {
		return false;
	}

	JPH::Vec3 moment = JPH::Vec3::sZero();
	JPH::Vec3 momentum = JPH::Vec3::sZero();

	for (uint32 i = 0; i < mSegments.Size; i++) {
		moment += states[i].Center * mSegments[i].Mass;
		momentum += states[i].Velocity * mSegments[i].Mass;
	}

	out_center = moment / mTotalMass;
	out_velocity = momentum / mTotalMass;

	return true;
}

bool Ragdoll::GetBodyGroundContact(uint32 body_index, BodyGroundContact& out_contact) const
{
	if (mpRagdoll == nullptr || body_index >= mSegments.Size) {
		out_contact = BodyGroundContact {};
		return false;
	}

	gPhysics->pBackend->GetBodyGroundContact(GetBodyID(body_index), out_contact);

	return out_contact.Count > 0;
}

bool Ragdoll::IsSupportNormal(const BodyGroundContact& contact, const JPH::Vec3 up, float32 min_up)
{
	return FVec3::ToJolt(contact.Normal).Dot(up) >= min_up;
}

bool Ragdoll::IsBodyOnGround(uint32 body_index, const JPH::Vec3 up, float32 min_up) const
{
	BodyGroundContact contact;

	return GetBodyGroundContact(body_index, contact) && IsSupportNormal(contact, up, min_up);
}

Mat4f Ragdoll::GetSegmentWorldMatrix(uint32 body_index, float32 step_alpha) const
{
	JPH::RVec3 current_position;
	JPH::Quat orientation;
	gPhysics->pBackend->GetBodyInterface().GetPositionAndRotation(GetBodyID(body_index), current_position, orientation);

	JPH::Vec3 position = JPH::Vec3(current_position);
	const BodyPose& previous = mPreviousPoses[body_index];

	// Physics steps at a fixed rate, so draw the body between its last two steps or it judders at other frame rates
	if (previous.bValid) {
		position = previous.Position + (position - previous.Position) * step_alpha;
		orientation = previous.Orientation.SLERP(orientation, step_alpha).Normalized();
	}

	const Mat4f world = MatrixFromRotationTranslation(JPH::Mat44::sRotation(orientation), position);

	Mat4f model = world * mWorldInverse;
	NormalizeRotationRows(model);
	ScaleRotationRows(model, mSegments[body_index].BoneScale);

	return model;
}

void Ragdoll::WriteToSkeleton()
{
	if (mpRagdoll == nullptr || !mpSkeleton.IsValid()) {
		return;
	}

	const float32 step_alpha = gPhysics->pBackend->GetStepAlpha();

	if (mbLimp) {
		const bool is_asleep = !mpRagdoll->IsActive();

		if (is_asleep && mbPoseSettled) {
			return;
		}

		mbPoseSettled = is_asleep;

		for (uint32 i = 0; i < mSegments.Size; i++) {
			mDrivenWorld[mSegments[i].BoneIndex] = GetSegmentWorldMatrix(i, step_alpha);
			mIsDriven[mSegments[i].BoneIndex] = 1;
		}

		mpSkeleton->PoseFromDrivenBones(mDrivenWorld.pData, mIsDriven.pData);
		return;
	}

	const Skeleton& skel = *mpSkeleton;
	bool any_driven = false;

	for (uint32 i = 0; i < mSegments.Size; i++) {
		const RDSegmentInfo& info = mSegments[i];

		if (mPoseBlend > 0.0f && info.Mode == eRagdollBodyMode::Kinematic) {
			JPH::Vec3 position;
			JPH::Quat orientation;
			GetPoseBlendWorld(i, position, orientation);

			Mat4f model = MatrixFromRotationTranslation(JPH::Mat44::sRotation(orientation), position) * mWorldInverse;
			NormalizeRotationRows(model);
			ScaleRotationRows(model, info.BoneScale);

			mIsDriven[info.BoneIndex] = 1;
			mDrivenWorld[info.BoneIndex] = model;
			any_driven = true;
			continue;
		}

		const bool is_driven = (info.Mode != eRagdollBodyMode::Kinematic) && (info.Blend > 0.0f);

		mIsDriven[info.BoneIndex] = static_cast<uint8>(is_driven);

		if (!is_driven) {
			continue;
		}

		any_driven = true;

		Mat4f model = GetSegmentWorldMatrix(i, step_alpha);

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

	for (FootPlant& plant : mFootPlants) {
		if (mpPhysicsSystem != nullptr && plant.pConstraint != nullptr) {
			mpPhysicsSystem->RemoveConstraint(plant.pConstraint);
		}

		plant = FootPlant {};
	}

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
	mSegments.Clear();
	mSegmentTargets.Free();
	mSegmentStates.Free();
	mMotorOverrides.Free();
	mLayers.Free();
	mPreviousPoses.Free();
	mPoseBlendFrom.Free();
	mPoseBlend = 0.0f;
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
