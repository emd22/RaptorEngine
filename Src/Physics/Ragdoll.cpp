#include "Ragdoll.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <ThirdParty/Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>
#include <ThirdParty/Jolt/Skeleton/Skeleton.h>

#include <Color.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <algorithm>
#include <cmath>
#include <string_view>

namespace fx::physics {

namespace {

constexpr float32 scDegreesToRadians = 0.0174532925f;

constexpr float32 scMotorFrequency = 10.0f;
constexpr float32 scMotorDamping = 1.0f;
constexpr float32 scMotorMaxTorque = 150.0f;
constexpr float32 scMotorMinFrequencyScale = 0.2f;

constexpr float32 scTotalMass = 75.0f;
constexpr float32 scSelfCollisionMargin = 0.02f;
constexpr float32 scHingeSlackDegrees = 5.0f;
constexpr float32 scHingeMinBend = 0.17f;

enum class eJoint : uint8
{
	Cone,
	HingeForward,
	HingeBackward,
};

struct BoneDef
{
	const char* Bone;
	const char* EndBone;
	float32 ExtraLength;
	float32 Radius;
	float32 SwingDegrees;
	float32 TwistDegrees;
	bool Anchor;
	float32 MassFraction;
	eJoint Joint = eJoint::Cone;
	float32 FlexDegrees = 0.0f;
};

struct ResolvedBone
{
	const BoneDef* pDef;
	uint32 Bone;
	uint32 EndBone;
};

constexpr BoneDef scHumanoid[] = {
	{ "root", "spine04", 0.0f, 0.11f, 0.0f, 0.0f, true, 0.22f },
	{ "spine03", "neck01", 0.0f, 0.13f, 25.0f, 20.0f, false, 0.28f },
	{ "neck01", "head", 0.2f, 0.09f, 40.0f, 45.0f, false, 0.08f },

	{ "upperarm01.L", "lowerarm01.L", 0.0f, 0.05f, 95.0f, 50.0f, false, 0.028f },
	{ "lowerarm01.L", "wrist.L", 0.0f, 0.04f, 0.0f, 60.0f, false, 0.022f, eJoint::HingeForward, 140.0f },
	{ "upperarm01.R", "lowerarm01.R", 0.0f, 0.05f, 95.0f, 50.0f, false, 0.028f },
	{ "lowerarm01.R", "wrist.R", 0.0f, 0.04f, 0.0f, 60.0f, false, 0.022f, eJoint::HingeForward, 140.0f },

	{ "upperleg01.L", "lowerleg01.L", 0.0f, 0.075f, 70.0f, 30.0f, true, 0.10f },
	{ "lowerleg01.L", "foot.L", 0.0f, 0.055f, 0.0f, 10.0f, true, 0.046f, eJoint::HingeBackward, 135.0f },
	{ "foot.L", "toe1-1.L", 0.0f, 0.04f, 30.0f, 10.0f, true, 0.014f },
	{ "upperleg01.R", "lowerleg01.R", 0.0f, 0.075f, 70.0f, 30.0f, true, 0.10f },
	{ "lowerleg01.R", "foot.R", 0.0f, 0.055f, 0.0f, 10.0f, true, 0.046f, eJoint::HingeBackward, 135.0f },
	{ "foot.R", "toe1-1.R", 0.0f, 0.04f, 30.0f, 10.0f, true, 0.014f },
};

JPH::Vec3 RowAsVec3(const Mat4f& m, uint32 row)
{
	return JPH::Vec3(m.RawData[row * 4], m.RawData[row * 4 + 1], m.RawData[row * 4 + 2]);
}

JPH::Mat44 RotationFromMatrix(const Mat4f& m)
{
	const JPH::Vec3 x = RowAsVec3(m, 0).Normalized();

	JPH::Vec3 y = RowAsVec3(m, 1);
	y = (y - x * y.Dot(x)).Normalized();

	const JPH::Vec3 z = x.Cross(y);

	return JPH::Mat44(JPH::Vec4(x, 0.0f), JPH::Vec4(y, 0.0f), JPH::Vec4(z, 0.0f), JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

Mat4f MatrixFromRotationTranslation(const JPH::Mat44& rotation, const JPH::Vec3& position)
{
	const JPH::Vec3 x = rotation.GetAxisX();
	const JPH::Vec3 y = rotation.GetAxisY();
	const JPH::Vec3 z = rotation.GetAxisZ();

	return Mat4f(Vec4f(x.GetX(), x.GetY(), x.GetZ(), 0.0f), Vec4f(y.GetX(), y.GetY(), y.GetZ(), 0.0f),
				 Vec4f(z.GetX(), z.GetY(), z.GetZ(), 0.0f),
				 Vec4f(position.GetX(), position.GetY(), position.GetZ(), 1.0f));
}

void NormalizeRotationRows(Mat4f& m)
{
	for (uint32 row = 0; row < 3; row++) {
		float32* r = &m.RawData[row * 4];
		const float32 length = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);

		if (length > 1e-6f) {
			r[0] /= length;
			r[1] /= length;
			r[2] /= length;
		}

		r[3] = 0.0f;
	}
}

uint32 FindBoneIndex(const Skeleton& skeleton, const char* name)
{
	const String bone_name(name);

	for (uint32 i = 0; i < skeleton.JointCount; i++) {
		if (skeleton.BoneNames[i] == bone_name) {
			return i;
		}
	}

	return BoneNull;
}

} // namespace

bool Ragdoll::Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix)
{
	Destroy();

	if (!skeleton.IsValid() || skeleton->JointCount == 0 || gPhysics == nullptr || gPhysics->pBackend == nullptr) {
		return false;
	}

	Skeleton& skel = *skeleton;

	const AnimationPlayback* playback = skel.GetActivePlayback();
	skel.EvaluatePose((playback != nullptr) ? playback->pAnimation : nullptr,
					  (playback != nullptr) ? playback->Time : 0.0f);

	const uint32 joint_count = skel.JointCount;
	const float32 scale = RowAsVec3(object_world_matrix, 0).Length();

	// std::vector<ResolvedBone> parts;
	std::vector<int> body_of_bone(joint_count, -1);

	SizedArray<ResolvedBone> parts(std::size(scHumanoid));

	for (const BoneDef& def : scHumanoid) {
		const uint32 bone_index = FindBoneIndex(skel, def.Bone);
		const uint32 end_bone_index = FindBoneIndex(skel, def.EndBone);

		if (bone_index == BoneNull || end_bone_index == BoneNull) {
			LogWarning(LC_PHYSICS, "Ragdoll: skeleton has no bone '{}' or '{}', skipping it", def.Bone, def.EndBone);
			continue;
		}

		body_of_bone[bone_index] = static_cast<int>(parts.Size);
		parts.Insert(ResolvedBone { .pDef = &def, .Bone = bone_index, .EndBone = end_bone_index });
	}

	if (parts.Size < 2) {
		LogWarning(LC_PHYSICS, "Ragdoll: skeleton does not look like a humanoid, not creating a ragdoll");
		return false;
	}

	const JPH::Vec3 up_world = (RowAsVec3(skel.WorldTransforms[parts[0].EndBone] * object_world_matrix, 3) -
								RowAsVec3(skel.WorldTransforms[parts[0].Bone] * object_world_matrix, 3))
								   .NormalizedOr(JPH::Vec3::sAxisY());

	JPH::Vec3 forward_world = JPH::Vec3::sZero();

	for (uint32 i = 0; i < parts.Size; i++) {
		if (std::string_view(parts[i].pDef->Bone).starts_with("foot")) {
			forward_world += RowAsVec3(skel.WorldTransforms[parts[i].EndBone] * object_world_matrix, 3) -
							 RowAsVec3(skel.WorldTransforms[parts[i].Bone] * object_world_matrix, 3);
		}
	}

	forward_world = (forward_world - up_world * forward_world.Dot(up_world)).NormalizedOr(JPH::Vec3::sZero());

	const float32 total_mass = scTotalMass * scale * scale * scale;

	JPH::Ref<JPH::Skeleton> jolt_skeleton = new JPH::Skeleton;
	JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings;

	settings->mSkeleton = jolt_skeleton;
	settings->mParts.resize(parts.Size);

	JPH::Array<JPH::Mat44> joint_matrices;
	joint_matrices.reserve(parts.Size);

	mBodies.InitCapacity(parts.Size);

	for (uint32 i = 0; i < parts.Size; i++) {
		const BoneDef& def = *parts[i].pDef;
		const uint32 bone = parts[i].Bone;
		const uint32 end_bone = parts[i].EndBone;

		int parent_body = -1;

		for (uint32 ancestor = skel.ParentIndices[bone]; ancestor != BoneNull;
			 ancestor = skel.ParentIndices[ancestor]) {
			if (body_of_bone[ancestor] >= 0) {
				parent_body = body_of_bone[ancestor];
				break;
			}
		}

		if (parent_body < 0 && i != 0) {
			LogWarning(LC_PHYSICS, "Ragdoll: bone '{}' has no ragdoll ancestor, not creating a ragdoll", def.Bone);
			return false;
		}

		jolt_skeleton->AddJoint(std::string_view(def.Bone), parent_body);

		const Mat4f bone_world = skel.WorldTransforms[bone] * object_world_matrix;
		const JPH::Mat44 rotation = RotationFromMatrix(bone_world);
		const JPH::Quat orientation = rotation.GetQuaternion().Normalized();
		const JPH::Vec3 position = RowAsVec3(bone_world, 3);

		const JPH::Vec3 end_position = RowAsVec3(skel.WorldTransforms[end_bone] * object_world_matrix, 3);
		JPH::Vec3 delta = end_position - position;
		const float32 span = delta.Length();

		if (span < 1e-4f) {
			LogWarning(LC_PHYSICS, "Ragdoll: bone '{}' has no length, not creating a ragdoll", def.Bone);
			return false;
		}

		const JPH::Vec3 axis_world = delta / span;
		const float32 length = span + def.ExtraLength * scale;
		const float32 radius = def.Radius * scale;
		const float32 half_height = std::max(length * 0.5f - radius, 0.01f * scale);

		const JPH::Vec3 axis_local = rotation.Multiply3x3Transposed(axis_world);
		const JPH::Vec3 center_local = axis_local * (length * 0.5f);

		JPH::ShapeRefC capsule = JPH::CapsuleShapeSettings(half_height, radius).Create().Get();
		JPH::ShapeRefC shape = JPH::RotatedTranslatedShapeSettings(
								   center_local, JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis_local), capsule)
								   .Create()
								   .Get();

		JPH::RagdollSettings::Part& part = settings->mParts[i];
		part.SetShape(shape);
		part.mPosition = position;
		part.mRotation = orientation;
		part.mMotionType = JPH::EMotionType::Dynamic;
		part.mObjectLayer = PhLayer::Dynamic;
		part.mFriction = 0.8f;
		part.mRestitution = 0.0f;
		part.mLinearDamping = 0.05f;
		part.mAngularDamping = 0.3f;
		part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		part.mMassPropertiesOverride.mMass = def.MassFraction * total_mass;

		if (parent_body >= 0) {
			JPH::SwingTwistConstraintSettings* joint = new JPH::SwingTwistConstraintSettings;
			joint->mSpace = JPH::EConstraintSpace::WorldSpace;
			joint->mPosition1 = joint->mPosition2 = position;
			joint->mTwistAxis1 = joint->mTwistAxis2 = axis_world;
			joint->mPlaneAxis1 = joint->mPlaneAxis2 = axis_world.GetNormalizedPerpendicular();
			joint->mNormalHalfConeAngle = joint->mPlaneHalfConeAngle = def.SwingDegrees * scDegreesToRadians;
			joint->mTwistMinAngle = -def.TwistDegrees * scDegreesToRadians;
			joint->mTwistMaxAngle = def.TwistDegrees * scDegreesToRadians;
			joint->mMaxFrictionTorque = 0.5f;
			joint->mSwingMotorSettings = JPH::MotorSettings(scMotorFrequency, scMotorDamping);
			joint->mTwistMotorSettings = JPH::MotorSettings(scMotorFrequency, scMotorDamping);

			if (def.Joint != eJoint::Cone) {
				const float32 flex = def.FlexDegrees * scDegreesToRadians;
				const float32 slack = scHingeSlackDegrees * scDegreesToRadians;

				const ResolvedBone& parent_part = parts[parent_body];
				const JPH::Vec3 parent_axis = (RowAsVec3(skel.WorldTransforms[parent_part.EndBone] * object_world_matrix, 3) -
											   RowAsVec3(skel.WorldTransforms[parent_part.Bone] * object_world_matrix, 3))
												  .NormalizedOr(axis_world);

				const JPH::Vec3 flex_direction = (def.Joint == eJoint::HingeForward) ? forward_world : -forward_world;
				const JPH::Vec3 bend_axis = parent_axis.Cross(axis_world);

				JPH::Vec3 hinge = axis_world.Cross(flex_direction);

				if (bend_axis.Length() > scHingeMinBend && (hinge.LengthSq() < 0.01f || bend_axis.Dot(hinge) > 0.0f)) {
					hinge = bend_axis;
				}

				if (hinge.LengthSq() > 0.01f) {
					hinge = hinge.Normalized();

					const float32 rest = std::atan2(hinge.Dot(parent_axis.Cross(axis_world)), parent_axis.Dot(axis_world));
					const float32 middle = (flex - slack) * 0.5f;

					joint->mTwistAxis1 = JPH::Quat::sRotation(hinge, middle - rest) * axis_world;
					joint->mPlaneAxis1 = joint->mPlaneAxis2 = hinge;
					joint->mNormalHalfConeAngle = (flex + slack) * 0.5f;
					joint->mPlaneHalfConeAngle = slack;
				}
				else {
					joint->mNormalHalfConeAngle = joint->mPlaneHalfConeAngle = flex * 0.5f;
				}
			}

			part.mToParent = joint;
		}

		joint_matrices.push_back(JPH::Mat44::sRotationTranslation(orientation, position));

		mBodies.Insert(BodyInfo { .BoneIndex = bone,
								  .LocalAxis = axis_local,
								  .LocalCenter = center_local,
								  .Length = length,
								  .Radius = radius,
								  .ParentBody = parent_body,
								  .bAnchor = def.Anchor,
								  .Mass = def.MassFraction * total_mass });
	}

	if (!settings->Stabilize()) {
		LogWarning(LC_PHYSICS, "Ragdoll: failed to stabilize the constraints");
	}

	settings->DisableParentChildCollisions(joint_matrices.data(), scSelfCollisionMargin * scale);

	static uint32 sNextGroupID = 0;

	JPH::PhysicsSystem& system = gPhysics->pBackend->PhysicsSystem;
	const uint32 serial = ++sNextGroupID;
	JPH::Ragdoll* ragdoll = settings->CreateRagdoll(serial, RagdollUserData::scTag | serial, &system);

	if (ragdoll == nullptr) {
		LogWarning(LC_PHYSICS, "Ragdoll: the physics system is out of bodies");
		return false;
	}

	mpRagdoll = ragdoll;
	mSerial = serial;
	mpRagdoll->AddToPhysicsSystem(JPH::EActivation::DontActivate);

	mbHasMotors = (mpRagdoll->GetConstraintCount() + 1 == mBodies.Size);

	for (BodyInfo& info : mBodies) {
		if (info.ParentBody >= 0) {
			const JPH::RagdollSettings::Part& parent = settings->mParts[info.ParentBody];
			const JPH::RagdollSettings::Part& part = settings->mParts[&info - mBodies.pData];

			info.ParentAnchor = parent.mRotation.Conjugated() * JPH::Vec3(part.mPosition - parent.mPosition);
		}
	}
	mTargetOrientations.assign(mBodies.Size, JPH::Quat::sIdentity());
	mTargetPositions.assign(mBodies.Size, JPH::Vec3::sZero());

	mpSkeleton = skeleton;
	// mBodies = std::move(bodies);
	mWorldInverse = object_world_matrix.Inverse();

	mDrivenWorld.assign(joint_count, Mat4f::scIdentity);
	mIsDriven.assign(joint_count, 0);

	for (const BodyInfo& body : mBodies) {
		mIsDriven[body.BoneIndex] = 1;
	}

	SizedArray<Mat4f> expected;
	expected.InitSize(joint_count);

	for (uint32 i = 0; i < joint_count; i++) {
		expected[i] = skel.SkinningMatrices[i];
	}

	WriteToSkeleton();

	float32 worst_error = 0.0f;
	for (uint32 i = 0; i < joint_count; i++) {
		for (uint32 k = 0; k < 16; k++) {
			worst_error = std::max(worst_error, std::abs(skel.SkinningMatrices[i].RawData[k] - expected[i].RawData[k]));
		}
	}

	LogInfo(LC_PHYSICS, "Ragdoll: {} bodies, rest pose skinning error {:.5f}", mBodies.Size, worst_error);

	return true;
}

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

	mWorldInverse = object_world_matrix.Inverse();

	const Skeleton& skel = *mpSkeleton;
	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	const float32 move_time = std::max(delta_time, gPhysics->pBackend->cTimeStep);

	for (uint32 i = 0; i < mBodies.Size; i++) {
		BodyInfo& info = mBodies[i];

		const Mat4f bone_world = skel.WorldTransforms[info.BoneIndex] * object_world_matrix;
		const JPH::Quat orientation = RotationFromMatrix(bone_world).GetQuaternion().Normalized();
		const JPH::Vec3 position = RowAsVec3(bone_world, 3);

		mTargetOrientations[i] = orientation;
		mTargetPositions[i] = position;

		if (info.Mode == eRagdollBodyMode::Limp) {
			continue;
		}

		const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(i));

		if (info.Mode == eRagdollBodyMode::Kinematic) {
			if (teleport || info.bSnap) {
				body_interface.SetPositionAndRotation(body_id, position, orientation, JPH::EActivation::DontActivate);
				body_interface.SetLinearAndAngularVelocity(body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
				info.bSnap = false;
			}
			else {
				body_interface.MoveKinematic(body_id, position, orientation, move_time);
			}

			continue;
		}

		if (teleport) {
			body_interface.SetPositionAndRotation(body_id, position, orientation, JPH::EActivation::Activate);
			body_interface.SetLinearAndAngularVelocity(body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
		}

		if (mbHasMotors && info.ParentBody >= 0) {
			JPH::SwingTwistConstraint* joint = static_cast<JPH::SwingTwistConstraint*>(
				mpRagdoll->GetConstraint(static_cast<int>(i) - 1));

			joint->SetTargetOrientationBS(mTargetOrientations[info.ParentBody].Conjugated() * orientation);
		}
	}
}

void Ragdoll::GoLimp(const Mat4f& object_world_matrix)
{
	if (mpRagdoll == nullptr || mbLimp) {
		return;
	}

	mWorldInverse = object_world_matrix.Inverse();

	SetAllBodyModes(eRagdollBodyMode::Limp);

	for (BodyInfo& info : mBodies) {
		info.Blend = 1.0f;
	}

	mpRagdoll->Activate();

	mbLimp = true;
	mbPoseSettled = false;

	if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(true);
	}
}

void Ragdoll::ApplyMotor(uint32 body_index)
{
	const BodyInfo& info = mBodies[body_index];

	if (!mbHasMotors || info.ParentBody < 0) {
		return;
	}

	JPH::SwingTwistConstraint* joint = static_cast<JPH::SwingTwistConstraint*>(
		mpRagdoll->GetConstraint(static_cast<int>(body_index) - 1));

	const JPH::EMotorState state = (info.Mode == eRagdollBodyMode::Powered) ? JPH::EMotorState::Position
																			: JPH::EMotorState::Off;

	const float32 frequency = scMotorFrequency *
							  (scMotorMinFrequencyScale + (1.0f - scMotorMinFrequencyScale) * info.Strength);
	const float32 torque = scMotorMaxTorque * info.Strength;

	joint->GetSwingMotorSettings().mSpringSettings.mFrequency = frequency;
	joint->GetSwingMotorSettings().SetTorqueLimit(torque);
	joint->GetTwistMotorSettings().mSpringSettings.mFrequency = frequency;
	joint->GetTwistMotorSettings().SetTorqueLimit(torque);

	joint->SetSwingMotorState(state);
	joint->SetTwistMotorState(state);
}

void Ragdoll::SetBodyMode(uint32 body_index, eRagdollBodyMode mode)
{
	if (mpRagdoll == nullptr || body_index >= mBodies.Size || mBodies[body_index].Mode == mode) {
		return;
	}

	BodyInfo& info = mBodies[body_index];
	info.Mode = mode;

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(body_index));

	if (mode == eRagdollBodyMode::Kinematic) {
		body_interface.SetMotionType(body_id, JPH::EMotionType::Kinematic, JPH::EActivation::DontActivate);
		info.bSnap = true;
	}
	else {
		body_interface.SetMotionType(body_id, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
		body_interface.SetGravityFactor(body_id, (mode == eRagdollBodyMode::Powered) ? 0.0f : 1.0f);
		body_interface.ActivateBody(body_id);
	}

	ApplyMotor(body_index);
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

	mBodies[body_index].Strength = std::clamp(strength, 0.0f, 1.0f);
	ApplyMotor(body_index);
}

void Ragdoll::SetBodyBlend(uint32 body_index, float32 blend)
{
	if (body_index < mBodies.Size) {
		mBodies[body_index].Blend = std::clamp(blend, 0.0f, 1.0f);
	}
}

int32 Ragdoll::FindBodyIndex(JPH::BodyID body_id) const
{
	if (mpRagdoll == nullptr) {
		return -1;
	}

	for (uint32 i = 0; i < mBodies.Size; i++) {
		if (mpRagdoll->GetBodyID(static_cast<int>(i)) == body_id) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

JPH::BodyID Ragdoll::GetBodyID(uint32 body_index) const { return mpRagdoll->GetBodyID(static_cast<int>(body_index)); }

float32 Ragdoll::GetBodyMass(uint32 body_index) const
{
	return (body_index < mBodies.Size) ? mBodies[body_index].Mass : 0.0f;
}

bool Ragdoll::GetBodyTrackingError(uint32 body_index, float32& out_distance, float32& out_angle) const
{
	if (mpRagdoll == nullptr || body_index >= mBodies.Size || body_index >= mTargetPositions.size()) {
		return false;
	}

	JPH::RVec3 position;
	JPH::Quat orientation;
	gPhysics->pBackend->GetBodyInterface().GetPositionAndRotation(GetBodyID(body_index), position, orientation);

	const float32 alignment = std::min(std::abs(orientation.Dot(mTargetOrientations[body_index])), 1.0f);

	out_distance = (JPH::Vec3(position) - mTargetPositions[body_index]).Length();
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

Vec3f Ragdoll::GetBodyPosition(uint32 body_index) const
{
	const JPH::RVec3 position = gPhysics->pBackend->GetBodyInterface().GetCenterOfMassPosition(GetBodyID(body_index));
	return Vec3f(position.GetX(), position.GetY(), position.GetZ());
}

Mat4f Ragdoll::GetBodyModelMatrix(uint32 body_index) const
{
	JPH::RVec3 position;
	JPH::Quat orientation;
	gPhysics->pBackend->GetBodyInterface().GetPositionAndRotation(GetBodyID(body_index), position, orientation);

	const Mat4f world = MatrixFromRotationTranslation(JPH::Mat44::sRotation(orientation), JPH::Vec3(position));

	Mat4f model = world * mWorldInverse;
	NormalizeRotationRows(model);

	return model;
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

void Ragdoll::Destroy()
{
	if (mpRagdoll != nullptr) {
		mpRagdoll->RemoveFromPhysicsSystem();
		mpRagdoll = nullptr;
	}

	if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(false);
	}

	mpSkeleton = nullptr;
	mBodies.Clear();
	mDrivenWorld.clear();
	mIsDriven.clear();
	mTargetOrientations.clear();
	mTargetPositions.clear();
	mbPoseSettled = false;
	mbLimp = false;
	mbHasMotors = false;
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

		mpSkeleton->PoseFromDrivenBones(mDrivenWorld.data(), mIsDriven.data());
		return;
	}

	const Skeleton& skel = *mpSkeleton;
	bool any_driven = false;

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const BodyInfo& info = mBodies[i];
		const bool is_driven = (info.Mode != eRagdollBodyMode::Kinematic) && (info.Blend > 0.0f);

		mIsDriven[info.BoneIndex] = is_driven ? 1 : 0;

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
		}

		mDrivenWorld[info.BoneIndex] = model;
	}

	if (any_driven) {
		mpSkeleton->PoseFromDrivenBones(mDrivenWorld.data(), mIsDriven.data());
	}
}

bool Ragdoll::FindBoneForBody(JPH::BodyID body_id, uint32& out_bone_index) const
{
	if (mpRagdoll == nullptr) {
		return false;
	}

	for (uint32 i = 0; i < mBodies.Size; i++) {
		if (mpRagdoll->GetBodyID(static_cast<int>(i)) == body_id) {
			out_bone_index = mBodies[i].BoneIndex;
			return true;
		}
	}

	return false;
}

eRagdollBodyPart Ragdoll::GetBodyPart(uint32 body_index) const
{
	if (!mpSkeleton.IsValid() || body_index >= mBodies.Size || mBodies[body_index].BoneIndex >= mpSkeleton->JointCount) {
		return eRagdollBodyPart::Torso;
	}

	const std::string_view name(mpSkeleton->BoneNames[mBodies[body_index].BoneIndex].CStr());

	if (name == "neck01" || name == "head") {
		return eRagdollBodyPart::Head;
	}

	if (name.starts_with("upperarm") || name.starts_with("lowerarm")) {
		return eRagdollBodyPart::Arm;
	}

	if (name.starts_with("upperleg") || name.starts_with("lowerleg") || name.starts_with("foot")) {
		return eRagdollBodyPart::Leg;
	}

	return eRagdollBodyPart::Torso;
}

void Ragdoll::DebugDraw() const
{
	if (mpRagdoll == nullptr) {
		return;
	}

	static const Color scPartColors[] = {
		Color::FromRGBA(235, 235, 235, 255),
		Color::FromRGBA(255, 60, 60, 255),
		Color::FromRGBA(70, 170, 255, 255),
		Color::FromRGBA(80, 230, 90, 255),
	};
	static const Color scLinkColor = Color::FromRGBA(255, 220, 60, 255);
	static const Color scVelocityColor = Color::FromRGBA(255, 70, 220, 255);

	constexpr float32 scKinematicDim = 0.45f;
	constexpr float32 scMinVelocityDraw = 0.5f;
	constexpr float32 scVelocityScale = 0.15f;

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	std::vector<Vec3f> centers(mBodies.Size, Vec3f::sZero);

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const BodyInfo& info = mBodies[i];
		const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(i));

		JPH::RVec3 position;
		JPH::Quat orientation;
		body_interface.GetPositionAndRotation(body_id, position, orientation);

		const JPH::Mat44 rotation = JPH::Mat44::sRotation(orientation);

		const JPH::Vec3 axis = rotation * info.LocalAxis;
		const JPH::Vec3 center = JPH::Vec3(position) + rotation * info.LocalCenter;
		const JPH::Vec3 side = axis.GetNormalizedPerpendicular();
		const JPH::Vec3 front = side.Cross(axis);

		const JPH::Vec3 x = side * info.Radius;
		const JPH::Vec3 y = axis * (info.Length * 0.5f);
		const JPH::Vec3 z = front * info.Radius;

		const Mat4f box(Vec4f(x.GetX(), x.GetY(), x.GetZ(), 0.0f), Vec4f(y.GetX(), y.GetY(), y.GetZ(), 0.0f),
						Vec4f(z.GetX(), z.GetY(), z.GetZ(), 0.0f),
						Vec4f(center.GetX(), center.GetY(), center.GetZ(), 1.0f));

		centers[i] = Vec3f(center.GetX(), center.GetY(), center.GetZ());

		Color color = scPartColors[static_cast<uint32>(GetBodyPart(i))];

		if (info.Mode == eRagdollBodyMode::Kinematic) {
			color = Color::FromRGBA(static_cast<uint8>(color.R * scKinematicDim),
									static_cast<uint8>(color.G * scKinematicDim),
									static_cast<uint8>(color.B * scKinematicDim), 255);
		}

		renderer::gDebugDraw->WireBox(box, color);

		const JPH::Vec3 velocity = body_interface.GetLinearVelocity(body_id);

		if (velocity.Length() > scMinVelocityDraw) {
			renderer::gDebugDraw->Line(centers[i], centers[i] + Vec3f(velocity) * scVelocityScale, scVelocityColor);
		}
	}

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const int32 parent = mBodies[i].ParentBody;

		if (parent >= 0) {
			renderer::gDebugDraw->Line(centers[i], centers[parent], scLinkColor);
		}
	}
}

} // namespace fx::physics
