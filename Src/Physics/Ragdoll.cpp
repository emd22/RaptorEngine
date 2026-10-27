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

constexpr float32 scMotorMinFrequencyScale = 0.2f;

constexpr float32 scSelfCollisionMargin = 0.02f;
constexpr float32 scHingeSlackDegrees = 5.0f;
constexpr float32 scHingeMinBend = 0.17f;

constexpr float32 scSupportNormalMinUp = 0.7f;
constexpr float32 scComCrossSize = 0.08f;

struct ResolvedBone
{
	const RagdollBodyDef* pDef;
	uint32 Bone;
	uint32 EndBone;
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

JPH::Vec3 GetUpDirection()
{
	const JPH::Vec3 gravity = gPhysics->pBackend->PhysicsSystem.GetGravity();
	return (-gravity).NormalizedOr(JPH::Vec3::sAxisY());
}

Color GetTorqueColor(float32 fraction)
{
	const float32 t = std::clamp(fraction, 0.0f, 1.0f);

	return Color::FromRGBA(static_cast<uint8>(70.0f + 185.0f * t), static_cast<uint8>(230.0f - 170.0f * t), 90, 255);
}

void ConvexHullOnPlane(const std::vector<JPH::Vec3>& points, const JPH::Vec3 up, std::vector<JPH::Vec3>& out_hull)
{
	out_hull.clear();

	if (points.size() < 3) {
		out_hull = points;
		return;
	}

	struct PlanarPoint
	{
		JPH::Vec3 Point;
		float32 X;
		float32 Y;
	};

	const JPH::Vec3 axis_u = up.GetNormalizedPerpendicular();
	const JPH::Vec3 axis_v = up.Cross(axis_u);

	std::vector<PlanarPoint> sorted;
	sorted.reserve(points.size());

	for (const JPH::Vec3& point : points) {
		sorted.push_back(PlanarPoint { point, point.Dot(axis_u), point.Dot(axis_v) });
	}

	std::sort(sorted.begin(), sorted.end(), [](const PlanarPoint& a, const PlanarPoint& b) {
		return (a.X < b.X) || (a.X == b.X && a.Y < b.Y);
	});

	const auto cross = [](const PlanarPoint& origin, const PlanarPoint& a, const PlanarPoint& b) {
		return (a.X - origin.X) * (b.Y - origin.Y) - (a.Y - origin.Y) * (b.X - origin.X);
	};

	std::vector<uint32> hull;
	hull.reserve(sorted.size() * 2);

	for (uint32 i = 0; i < sorted.size(); i++) {
		while (hull.size() >= 2 && cross(sorted[hull[hull.size() - 2]], sorted[hull.back()], sorted[i]) <= 0.0f) {
			hull.pop_back();
		}
		hull.push_back(i);
	}

	const size_t lower_size = hull.size() + 1;

	for (size_t i = sorted.size() - 1; i > 0; i--) {
		const uint32 index = static_cast<uint32>(i - 1);

		while (hull.size() >= lower_size && cross(sorted[hull[hull.size() - 2]], sorted[hull.back()], sorted[index]) <= 0.0f) {
			hull.pop_back();
		}
		hull.push_back(index);
	}

	hull.pop_back();

	for (const uint32 index : hull) {
		out_hull.push_back(sorted[index].Point);
	}
}

} // namespace

bool Ragdoll::Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix, const RagdollDef& def)
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

	std::vector<int> body_of_bone(joint_count, -1);

	SizedArray<ResolvedBone> parts(def.Bodies.size());

	for (const RagdollBodyDef& body_def : def.Bodies) {
		const uint32 bone_index = FindBoneIndex(skel, body_def.Bone.CStr());
		const uint32 end_bone_index = FindBoneIndex(skel, body_def.EndBone.CStr());

		if (bone_index == BoneNull || end_bone_index == BoneNull) {
			LogWarning(LC_PHYSICS, "Ragdoll: skeleton has no bone '{}' or '{}', skipping it", body_def.Bone.CStr(),
					   body_def.EndBone.CStr());
			continue;
		}

		body_of_bone[bone_index] = static_cast<int>(parts.Size);
		parts.Insert(ResolvedBone { .pDef = &body_def, .Bone = bone_index, .EndBone = end_bone_index });
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
		if (std::string_view(parts[i].pDef->Bone.CStr()).starts_with("foot")) {
			forward_world += RowAsVec3(skel.WorldTransforms[parts[i].EndBone] * object_world_matrix, 3) -
							 RowAsVec3(skel.WorldTransforms[parts[i].Bone] * object_world_matrix, 3);
		}
	}

	forward_world = (forward_world - up_world * forward_world.Dot(up_world)).NormalizedOr(JPH::Vec3::sZero());

	const float32 total_mass = def.TotalMass * scale * scale * scale;

	JPH::Ref<JPH::Skeleton> jolt_skeleton = new JPH::Skeleton;
	JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings;

	settings->mSkeleton = jolt_skeleton;
	settings->mParts.resize(parts.Size);

	JPH::Array<JPH::Mat44> joint_matrices;
	joint_matrices.reserve(parts.Size);

	mBodies.InitCapacity(parts.Size);

	for (uint32 i = 0; i < parts.Size; i++) {
		const RagdollBodyDef& body_def = *parts[i].pDef;
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
			LogWarning(LC_PHYSICS, "Ragdoll: bone '{}' has no ragdoll ancestor, not creating a ragdoll",
					   body_def.Bone.CStr());
			return false;
		}

		jolt_skeleton->AddJoint(std::string_view(body_def.Bone.CStr()), parent_body);

		const Mat4f bone_world = skel.WorldTransforms[bone] * object_world_matrix;
		const JPH::Mat44 rotation = RotationFromMatrix(bone_world);
		const JPH::Quat orientation = rotation.GetQuaternion().Normalized();
		const JPH::Vec3 position = RowAsVec3(bone_world, 3);

		const JPH::Vec3 end_position = RowAsVec3(skel.WorldTransforms[end_bone] * object_world_matrix, 3);
		JPH::Vec3 delta = end_position - position;
		const float32 span = delta.Length();

		if (span < 1e-4f) {
			LogWarning(LC_PHYSICS, "Ragdoll: bone '{}' has no length, not creating a ragdoll", body_def.Bone.CStr());
			return false;
		}

		const JPH::Vec3 axis_world = delta / span;
		const float32 length = span + body_def.ExtraLength * scale;
		const float32 radius = body_def.Radius * scale;
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
		part.mMassPropertiesOverride.mMass = body_def.MassFraction * total_mass;

		if (parent_body >= 0) {
			JPH::SwingTwistConstraintSettings* joint = new JPH::SwingTwistConstraintSettings;
			joint->mSpace = JPH::EConstraintSpace::WorldSpace;
			joint->mPosition1 = joint->mPosition2 = position;
			joint->mTwistAxis1 = joint->mTwistAxis2 = axis_world;
			joint->mPlaneAxis1 = joint->mPlaneAxis2 = axis_world.GetNormalizedPerpendicular();
			joint->mNormalHalfConeAngle = joint->mPlaneHalfConeAngle = body_def.SwingDegrees * scDegreesToRadians;
			joint->mTwistMinAngle = -body_def.TwistDegrees * scDegreesToRadians;
			joint->mTwistMaxAngle = body_def.TwistDegrees * scDegreesToRadians;
			joint->mMaxFrictionTorque = 0.5f;
			joint->mSwingMotorSettings = JPH::MotorSettings(body_def.Muscle.Stiffness, body_def.Muscle.Damping);
			joint->mTwistMotorSettings = JPH::MotorSettings(body_def.Muscle.Stiffness, body_def.Muscle.Damping);

			if (body_def.Joint != eRagdollJoint::Cone) {
				const float32 flex = body_def.FlexDegrees * scDegreesToRadians;
				const float32 slack = scHingeSlackDegrees * scDegreesToRadians;

				const ResolvedBone& parent_part = parts[parent_body];
				const JPH::Vec3 parent_axis = (RowAsVec3(skel.WorldTransforms[parent_part.EndBone] * object_world_matrix, 3) -
											   RowAsVec3(skel.WorldTransforms[parent_part.Bone] * object_world_matrix, 3))
												  .NormalizedOr(axis_world);

				const JPH::Vec3 flex_direction = (body_def.Joint == eRagdollJoint::HingeForward) ? forward_world : -forward_world;
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
								  .bAnchor = body_def.bAnchor,
								  .Mass = body_def.MassFraction * total_mass,
								  .Muscle = body_def.Muscle });
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

	mpPhysicsSystem = &system;
	system.AddStepListener(this);

	mbHasMotors = (mpRagdoll->GetConstraintCount() + 1 == mBodies.Size);

	for (BodyInfo& info : mBodies) {
		if (info.ParentBody >= 0) {
			const JPH::RagdollSettings::Part& parent = settings->mParts[info.ParentBody];
			const JPH::RagdollSettings::Part& part = settings->mParts[&info - mBodies.pData];

			info.ParentAnchor = parent.mRotation.Conjugated() * JPH::Vec3(part.mPosition - parent.mPosition);
		}
	}

	mTargets.assign(mBodies.Size, BodyTarget {});

	mpSkeleton = skeleton;
	mWorldInverse = object_world_matrix.AffineInverse();

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

	LogInfo(LC_PHYSICS, "Ragdoll: {} bodies, {:.3f} kg, rest pose skinning error {:.5f}", mBodies.Size, total_mass,
			worst_error);

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

void Ragdoll::Drive(const Mat4f& object_world_matrix, bool teleport)
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
		target.Orientation = RotationFromMatrix(bone_world).GetQuaternion().Normalized();
		target.Position = RowAsVec3(bone_world, 3);

		if (info.Mode == eRagdollBodyMode::Limp) {
			continue;
		}

		const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(i));

		if (info.Mode == eRagdollBodyMode::Kinematic) {
			if (teleport || info.bSnap) {
				body_interface.SetPositionAndRotation(body_id, target.Position, target.Orientation,
													  JPH::EActivation::DontActivate);
				body_interface.SetLinearAndAngularVelocity(body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
				info.bSnap = false;
			}

			continue;
		}

		if (teleport) {
			body_interface.SetPositionAndRotation(body_id, target.Position, target.Orientation, JPH::EActivation::Activate);
			body_interface.SetLinearAndAngularVelocity(body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
		}
	}
}

void Ragdoll::GoLimp(const Mat4f& object_world_matrix)
{
	if (mpRagdoll == nullptr || mbLimp) {
		return;
	}

	mWorldInverse = object_world_matrix.AffineInverse();

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

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const BodyTarget& target = mTargets[i];

		if (target.Mode == eRagdollBodyMode::Kinematic) {
			const JPH::BodyID body_id = mpRagdoll->GetBodyID(static_cast<int>(i));

			JPH::RVec3 position;
			JPH::Quat orientation;
			body_interface.GetPositionAndRotation(body_id, position, orientation);

			const JPH::Vec3 step_position = JPH::Vec3(position) + (target.Position - JPH::Vec3(position)) * fraction;
			const JPH::Quat step_orientation = orientation.SLERP(target.Orientation, fraction).Normalized();

			body_interface.MoveKinematic(body_id, step_position, step_orientation, inContext.mDeltaTime);
		}

		ApplyMotor(i);
	}
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

	const bool bPowered = (target.Mode == eRagdollBodyMode::Powered);
	const JPH::EMotorState state = bPowered ? JPH::EMotorState::Position : JPH::EMotorState::Off;

	const float32 frequency = info.Muscle.Stiffness *
							  (scMotorMinFrequencyScale + (1.0f - scMotorMinFrequencyScale) * target.Strength);
	const float32 torque = info.Muscle.MaxTorque * target.Strength;

	joint->GetSwingMotorSettings().mSpringSettings.mFrequency = frequency;
	joint->GetSwingMotorSettings().SetTorqueLimit(torque);
	joint->GetTwistMotorSettings().mSpringSettings.mFrequency = frequency;
	joint->GetTwistMotorSettings().SetTorqueLimit(torque);

	joint->SetSwingMotorState(state);
	joint->SetTwistMotorState(state);

	if (bPowered) {
		joint->SetTargetOrientationBS(mTargets[info.ParentBody].Orientation.Conjugated() * target.Orientation);
	}
}

float32 Ragdoll::GetMotorTorqueFraction(uint32 body_index) const
{
	const BodyInfo& info = mBodies[body_index];

	if (!mbHasMotors || info.ParentBody < 0) {
		return 0.0f;
	}

	const float32 limit = info.Muscle.MaxTorque * mTargets[body_index].Strength;

	if (limit <= 0.0f) {
		return 0.0f;
	}

	const JPH::SwingTwistConstraint* joint = static_cast<const JPH::SwingTwistConstraint*>(
		mpRagdoll->GetConstraint(static_cast<int>(body_index) - 1));

	const JPH::Vec3 torque = joint->GetTotalLambdaMotor() / gPhysics->pBackend->GetCollisionStepTime();

	return torque.Length() / limit;
}

void Ragdoll::SetBodyMode(uint32 body_index, eRagdollBodyMode mode)
{
	if (mpRagdoll == nullptr || body_index >= mBodies.Size || mBodies[body_index].Mode == mode) {
		return;
	}

	BodyInfo& info = mBodies[body_index];
	info.Mode = mode;
	mTargets[body_index].Mode = mode;

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
	mTargets[body_index].Strength = mBodies[body_index].Strength;
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

Vec3f Ragdoll::GetBodyPosition(uint32 body_index) const
{
	const JPH::RVec3 position = gPhysics->pBackend->GetBodyInterface().GetCenterOfMassPosition(GetBodyID(body_index));
	return Vec3f(position.GetX(), position.GetY(), position.GetZ());
}

Vec3f Ragdoll::GetCenterOfMass() const
{
	JPH::Vec3 weighted_sum = JPH::Vec3::sZero();
	float32 total_mass = 0.0f;

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const Vec3f position = GetBodyPosition(i);
		weighted_sum += JPH::Vec3(position.X, position.Y, position.Z) * mBodies[i].Mass;
		total_mass += mBodies[i].Mass;
	}

	if (total_mass <= 0.0f) {
		return Vec3f::sZero;
	}

	return Vec3f(weighted_sum / total_mass);
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
	mTargets.clear();
	mDrivenWorld.clear();
	mIsDriven.clear();
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

	if (name.starts_with("upperarm") || name.starts_with("lowerarm") || name.starts_with("wrist")) {
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
	static const Color scComColor = Color::FromRGBA(255, 255, 255, 255);
	static const Color scSupportColor = Color::FromRGBA(90, 220, 255, 255);

	constexpr float32 scKinematicDim = 0.45f;
	constexpr float32 scMinVelocityDraw = 0.5f;
	constexpr float32 scVelocityScale = 0.15f;

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	const JPH::Vec3 up = GetUpDirection();

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
			color = Color::FromRGBA(static_cast<uint8>(color.R * scKinematicDim), static_cast<uint8>(color.G * scKinematicDim),
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

		if (parent < 0) {
			continue;
		}

		Color link_color = scLinkColor;

		if (mBodies[i].Mode == eRagdollBodyMode::Powered) {
			link_color = GetTorqueColor(GetMotorTorqueFraction(i));
		}

		renderer::gDebugDraw->Line(centers[i], centers[parent], link_color);
	}

	std::vector<JPH::Vec3> support_points;
	float32 support_height_sum = 0.0f;

	for (uint32 i = 0; i < mBodies.Size; i++) {
		BodyGroundContact contact;

		if (!GetBodyGroundContact(i, contact)) {
			continue;
		}

		const JPH::Vec3 normal(contact.Normal.X, contact.Normal.Y, contact.Normal.Z);

		if (normal.Dot(up) < scSupportNormalMinUp) {
			continue;
		}

		const JPH::Vec3 point(contact.Point.X, contact.Point.Y, contact.Point.Z);

		support_points.push_back(point);
		support_height_sum += point.Dot(up);
	}

	const Vec3f center_of_mass = GetCenterOfMass();

	renderer::gDebugDraw->Line(center_of_mass - Vec3f::sRight * scComCrossSize,
							   center_of_mass + Vec3f::sRight * scComCrossSize, scComColor);
	renderer::gDebugDraw->Line(center_of_mass - Vec3f::sUp * scComCrossSize, center_of_mass + Vec3f::sUp * scComCrossSize,
							   scComColor);

	if (support_points.empty()) {
		return;
	}

	const float32 support_height = support_height_sum / static_cast<float32>(support_points.size());
	const JPH::Vec3 com(center_of_mass.X, center_of_mass.Y, center_of_mass.Z);
	const JPH::Vec3 com_on_support = com - up * (com.Dot(up) - support_height);

	renderer::gDebugDraw->Line(center_of_mass, Vec3f(com_on_support), scComColor);

	std::vector<JPH::Vec3> hull;
	ConvexHullOnPlane(support_points, up, hull);

	if (hull.size() < 2) {
		return;
	}

	for (size_t i = 0; i < hull.size(); i++) {
		const JPH::Vec3& from = hull[i];
		const JPH::Vec3& to = hull[(i + 1) % hull.size()];

		renderer::gDebugDraw->Line(Vec3f(from), Vec3f(to), scSupportColor);
	}
}

} // namespace fx::physics
