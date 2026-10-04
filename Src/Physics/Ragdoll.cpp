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

namespace fx::physics {

namespace {

constexpr float32 scDegreesToRadians = 0.0174532925f;

struct BoneDef
{
	const char* Bone;
	const char* EndBone;
	float32 ExtraLength;
	float32 Radius;
	float32 SwingDegrees;
	float32 TwistDegrees;
};

struct ResolvedBone
{
	const BoneDef* pDef;
	uint32 Bone;
	uint32 EndBone;
};

constexpr BoneDef scHumanoid[] = {
	{ "root", "spine04", 0.0f, 0.11f, 0.0f, 0.0f },
	{ "spine03", "neck01", 0.0f, 0.13f, 25.0f, 20.0f },
	{ "neck01", "head", 0.2f, 0.09f, 40.0f, 45.0f },

	{ "upperarm01.L", "lowerarm01.L", 0.0f, 0.05f, 95.0f, 50.0f },
	{ "lowerarm01.L", "wrist.L", 0.0f, 0.04f, 85.0f, 35.0f },
	{ "upperarm01.R", "lowerarm01.R", 0.0f, 0.05f, 95.0f, 50.0f },
	{ "lowerarm01.R", "wrist.R", 0.0f, 0.04f, 85.0f, 35.0f },

	{ "upperleg01.L", "lowerleg01.L", 0.0f, 0.075f, 70.0f, 30.0f },
	{ "lowerleg01.L", "foot.L", 0.0f, 0.055f, 60.0f, 15.0f },
	{ "foot.L", "toe1-1.L", 0.0f, 0.04f, 30.0f, 10.0f },
	{ "upperleg01.R", "lowerleg01.R", 0.0f, 0.075f, 70.0f, 30.0f },
	{ "lowerleg01.R", "foot.R", 0.0f, 0.055f, 60.0f, 15.0f },
	{ "foot.R", "toe1-1.R", 0.0f, 0.04f, 30.0f, 10.0f },
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

			part.mToParent = joint;
		}

		joint_matrices.push_back(JPH::Mat44::sRotationTranslation(orientation, position));

		mBodies.Insert(BodyInfo { .BoneIndex = bone,
								  .LocalAxis = axis_local,
								  .LocalCenter = center_local,
								  .Length = length,
								  .Radius = radius });
	}

	if (!settings->Stabilize()) {
		LogWarning(LC_PHYSICS, "Ragdoll: failed to stabilize the constraints");
	}

	settings->DisableParentChildCollisions(joint_matrices.data(), 0.0f);

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

void Ragdoll::Activate(const Vec3f& velocity)
{
	if (mpRagdoll == nullptr) {
		return;
	}

	mpRagdoll->SetLinearVelocity(JPH::Vec3(velocity.X, velocity.Y, velocity.Z));
	mpRagdoll->Activate();

	if (mpSkeleton.IsValid()) {
		mpSkeleton->SetExternalPose(true);
	}
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
	mbPoseSettled = false;
}

void Ragdoll::WriteToSkeleton()
{
	if (mpRagdoll == nullptr || !mpSkeleton.IsValid()) {
		return;
	}

	const bool is_asleep = !mpRagdoll->IsActive();

	if (is_asleep && mbPoseSettled) {
		return;
	}

	mbPoseSettled = is_asleep;

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	for (uint32 i = 0; i < mBodies.Size; i++) {
		JPH::RVec3 position;
		JPH::Quat orientation;
		body_interface.GetPositionAndRotation(mpRagdoll->GetBodyID(static_cast<int>(i)), position, orientation);

		const Mat4f world = MatrixFromRotationTranslation(JPH::Mat44::sRotation(orientation), JPH::Vec3(position));

		Mat4f model = world * mWorldInverse;
		NormalizeRotationRows(model);

		mDrivenWorld[mBodies[i].BoneIndex] = model;
	}

	mpSkeleton->PoseFromDrivenBones(mDrivenWorld.data(), mIsDriven.data());
}

void Ragdoll::DebugDraw() const
{
	if (mpRagdoll == nullptr) {
		return;
	}

	const Color color = Color::FromRGBA(255, 160, 40, 255);
	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();

	for (uint32 i = 0; i < mBodies.Size; i++) {
		const BodyInfo& info = mBodies[i];

		JPH::RVec3 position;
		JPH::Quat orientation;
		body_interface.GetPositionAndRotation(mpRagdoll->GetBodyID(static_cast<int>(i)), position, orientation);

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

		renderer::gDebugDraw->WireBox(box, color);
	}
}

} // namespace fx::physics
