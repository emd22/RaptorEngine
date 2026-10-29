#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"
#include "Ragdoll.hpp"
#include "RagdollMath.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <ThirdParty/Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <ThirdParty/Jolt/Skeleton/Skeleton.h>

#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <algorithm>
#include <cmath>
#include <string_view>

namespace fx::physics {

using namespace RagdollMath;

namespace {

constexpr float32 scSelfCollisionMargin = 0.02f;
constexpr uint32 scSolverVelocitySteps = 20;
constexpr float32 scHingeSlackDegrees = 5.0f;
constexpr float32 scHingeMinBend = 0.17f;

struct ResolvedBone
{
	const RagdollBodyDef* pDef;
	uint32 Bone;
	uint32 EndBone;
};

struct BuildFrame
{
	const Skeleton& Skel;
	const Mat4f& ObjectWorld;

	float32 Scale = 1.0f;
	float32 TotalMass = 0.0f;

	JPH::Vec3 Up = JPH::Vec3::sAxisY();
	JPH::Vec3 Forward = JPH::Vec3::sZero();

	Mat4f GetBoneWorld(uint32 bone) const { return Skel.WorldTransforms[bone] * ObjectWorld; }
	JPH::Vec3 GetBonePosition(uint32 bone) const { return RowAsVec3(GetBoneWorld(bone), 3); }
	JPH::Vec3 GetBoneSpan(const ResolvedBone& part) const
	{
		return GetBonePosition(part.EndBone) - GetBonePosition(part.Bone);
	}
};

struct BodyShape
{
	JPH::ShapeRefC Shape;
	JPH::Vec3 CenterLocal = JPH::Vec3::sZero();
	bool bBox = false;
	JPH::Vec3 BoxHalfExtent = JPH::Vec3::sZero();
	JPH::Quat BoxRotation = JPH::Quat::sIdentity();
};

struct BuiltBody
{
	JPH::Vec3 Position = JPH::Vec3::sZero();
	JPH::Quat Orientation = JPH::Quat::sIdentity();
	JPH::Vec3 AxisLocal = JPH::Vec3::sAxisY();
	float32 Length = 0.0f;
	float32 Radius = 0.0f;
	BodyShape Shape;
	bool bHinge = false;
	JPH::Vec3 Hinge = JPH::Vec3::sZero();
};

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

void ResolveBones(const Skeleton& skel, const RagdollDef& def, SizedArray<ResolvedBone>& out_parts,
				  SizedArray<int32>& out_body_of_bone)
{
	InitFilled(out_body_of_bone, skel.JointCount, -1);
	out_parts.InitCapacity(def.Bodies.Size);

	for (const RagdollBodyDef& body_def : def.Bodies) {
		const uint32 bone_index = FindBoneIndex(skel, body_def.Bone.CStr());
		const uint32 end_bone_index = FindBoneIndex(skel, body_def.EndBone.CStr());

		if (bone_index == BoneNull || end_bone_index == BoneNull) {
			LogWarning(LC_PHYSICS, "Ragdoll: skeleton has no bone '{}' or '{}', skipping it", body_def.Bone.CStr(),
					   body_def.EndBone.CStr());
			continue;
		}

		out_body_of_bone[bone_index] = static_cast<int32>(out_parts.Size);
		out_parts.Insert(ResolvedBone { .pDef = &body_def, .Bone = bone_index, .EndBone = end_bone_index });
	}
}

int32 FindParentBody(const Skeleton& skel, const SizedArray<int32>& body_of_bone, uint32 bone)
{
	for (uint32 ancestor = skel.ParentIndices[bone]; ancestor != BoneNull; ancestor = skel.ParentIndices[ancestor]) {
		if (body_of_bone[ancestor] >= 0) {
			return body_of_bone[ancestor];
		}
	}

	return -1;
}

JPH::Vec3 GetRestForward(const BuildFrame& frame, const SizedArray<ResolvedBone>& parts)
{
	const JPH::Vec3 body_up = frame.GetBoneSpan(parts[0]).NormalizedOr(JPH::Vec3::sAxisY());

	JPH::Vec3 forward = JPH::Vec3::sZero();

	for (const ResolvedBone& part : parts) {
		if (part.pDef->bFoot) {
			forward += frame.GetBoneSpan(part);
		}
	}

	return (forward - body_up * forward.Dot(body_up)).NormalizedOr(JPH::Vec3::sZero());
}

BodyShape BuildCapsule(const JPH::Vec3 axis_local, float32 length, float32 radius, float32 scale)
{
	const float32 half_height = std::max(length * 0.5f - radius, 0.01f * scale);

	BodyShape shape;
	shape.CenterLocal = axis_local * (length * 0.5f);

	JPH::ShapeRefC capsule = JPH::CapsuleShapeSettings(half_height, radius).Create().Get();
	shape.Shape = JPH::RotatedTranslatedShapeSettings(shape.CenterLocal,
													  JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis_local), capsule)
					  .Create()
					  .Get();

	return shape;
}

BodyShape BuildFootBox(const BuildFrame& frame, const RagdollBodyDef& body_def, const JPH::Mat44& rotation,
					   const JPH::Vec3 position, const JPH::Vec3 end_position, const JPH::Vec3 axis_world,
					   float32 radius)
{
	const JPH::Vec3 foot_forward = (axis_world - frame.Up * axis_world.Dot(frame.Up)).NormalizedOr(frame.Forward);
	const JPH::Vec3 foot_side = frame.Up.Cross(foot_forward);

	const float32 top = position.Dot(frame.Up);
	const float32 bottom = std::min(top, end_position.Dot(frame.Up)) - radius;
	const float32 heel = -body_def.HeelLength * frame.Scale;
	const float32 toe = (end_position - position).Dot(foot_forward) + radius + body_def.ExtraLength * frame.Scale;

	const float32 box_half_height = std::max((top - bottom) * 0.5f, radius * 0.5f);

	BodyShape shape;
	shape.bBox = true;
	shape.BoxHalfExtent = JPH::Vec3(body_def.FootWidth * frame.Scale * 0.5f, box_half_height, (toe - heel) * 0.5f);

	const JPH::Vec3 center_world = position + foot_forward * ((heel + toe) * 0.5f) +
								   frame.Up * (bottom + box_half_height - top);

	const JPH::Mat44 box_world(JPH::Vec4(foot_side, 0.0f), JPH::Vec4(frame.Up, 0.0f), JPH::Vec4(foot_forward, 0.0f),
							   JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));

	shape.BoxRotation = (rotation.Transposed3x3() * box_world).GetQuaternion().Normalized();
	shape.CenterLocal = rotation.Multiply3x3Transposed(center_world - position);

	const float32 convex_radius = std::min(0.01f * frame.Scale, shape.BoxHalfExtent.ReduceMin() * 0.5f);

	JPH::ShapeRefC box = JPH::BoxShapeSettings(shape.BoxHalfExtent, convex_radius).Create().Get();
	shape.Shape = JPH::RotatedTranslatedShapeSettings(shape.CenterLocal, shape.BoxRotation, box).Create().Get();

	return shape;
}

bool ApplyHingeLimits(JPH::SwingTwistConstraintSettings& joint, const BuildFrame& frame, const RagdollBodyDef& body_def,
					  const ResolvedBone& parent_part, const JPH::Vec3 axis_world, JPH::Vec3& out_hinge)
{
	const float32 flex = MathUtil::DegToRad(body_def.FlexDegrees);
	const float32 slack = MathUtil::DegToRad(scHingeSlackDegrees);

	const JPH::Vec3 parent_axis = frame.GetBoneSpan(parent_part).NormalizedOr(axis_world);

	const JPH::Vec3 flex_direction = (body_def.Joint == eRagdollJoint::HingeForward) ? frame.Forward : -frame.Forward;
	const JPH::Vec3 bend_axis = parent_axis.Cross(axis_world);

	JPH::Vec3 hinge = axis_world.Cross(flex_direction);

	if (bend_axis.Length() > scHingeMinBend && (hinge.LengthSq() < 0.01f || bend_axis.Dot(hinge) > 0.0f)) {
		hinge = bend_axis;
	}

	if (hinge.LengthSq() > 0.01f) {
		hinge = hinge.Normalized();
		out_hinge = hinge;

		const float32 rest = std::atan2(hinge.Dot(parent_axis.Cross(axis_world)), parent_axis.Dot(axis_world));
		const float32 middle = (flex - slack) * 0.5f;

		joint.mTwistAxis1 = JPH::Quat::sRotation(hinge, middle - rest) * axis_world;
		joint.mPlaneAxis1 = joint.mPlaneAxis2 = hinge;
		joint.mNormalHalfConeAngle = (flex + slack) * 0.5f;
		joint.mPlaneHalfConeAngle = slack;

		return true;
	}

	joint.mNormalHalfConeAngle = joint.mPlaneHalfConeAngle = flex * 0.5f;

	return false;
}

JPH::SwingTwistConstraintSettings* BuildJoint(const BuildFrame& frame, const RagdollBodyDef& body_def,
											  const ResolvedBone& parent_part, const JPH::Vec3 position,
											  const JPH::Vec3 axis_world, BuiltBody& out_body)
{
	JPH::SwingTwistConstraintSettings* joint = new JPH::SwingTwistConstraintSettings;

	joint->mSpace = JPH::EConstraintSpace::WorldSpace;
	joint->mPosition1 = joint->mPosition2 = position;
	joint->mTwistAxis1 = joint->mTwistAxis2 = axis_world;
	joint->mPlaneAxis1 = joint->mPlaneAxis2 = axis_world.GetNormalizedPerpendicular();
	joint->mNormalHalfConeAngle = joint->mPlaneHalfConeAngle = MathUtil::DegToRad(body_def.SwingDegrees);
	joint->mTwistMinAngle = -MathUtil::DegToRad(body_def.TwistDegrees);
	joint->mTwistMaxAngle = MathUtil::DegToRad(body_def.TwistDegrees);
	joint->mMaxFrictionTorque = 0.5f;
	joint->mNumVelocityStepsOverride = scSolverVelocitySteps;
	joint->mSwingMotorSettings = JPH::MotorSettings(body_def.Muscle.Stiffness, body_def.Muscle.Damping);
	joint->mTwistMotorSettings = JPH::MotorSettings(body_def.Muscle.Stiffness, body_def.Muscle.Damping);

	if (body_def.Joint != eRagdollJoint::Cone) {
		out_body.bHinge = ApplyHingeLimits(*joint, frame, body_def, parent_part, axis_world, out_body.Hinge);
	}

	return joint;
}

void ConfigurePart(JPH::RagdollSettings::Part& part, const BuiltBody& body, const RagdollBodyDef& body_def,
				   float32 total_mass)
{
	part.SetShape(body.Shape.Shape);
	part.mPosition = body.Position;
	part.mRotation = body.Orientation;
	part.mMotionType = JPH::EMotionType::Dynamic;
	part.mObjectLayer = PhLayer::Dynamic;
	part.mFriction = body_def.Friction;
	part.mRestitution = 0.0f;
	part.mLinearDamping = 0.05f;
	part.mAngularDamping = 0.3f;
	part.mNumVelocityStepsOverride = scSolverVelocitySteps;
	part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
	part.mMassPropertiesOverride.mMass = body_def.MassFraction * total_mass;
}

bool BuildBody(const BuildFrame& frame, const SizedArray<ResolvedBone>& parts, uint32 body_index, int32 parent_body,
			   JPH::RagdollSettings& settings, BuiltBody& out_body)
{
	const RagdollBodyDef& body_def = *parts[body_index].pDef;

	const Mat4f bone_world = frame.GetBoneWorld(parts[body_index].Bone);
	const JPH::Mat44 rotation = RotationFromMatrix(bone_world);

	out_body.Orientation = rotation.GetQuaternion().Normalized();
	out_body.Position = RowAsVec3(bone_world, 3);

	const JPH::Vec3 end_position = frame.GetBonePosition(parts[body_index].EndBone);
	const JPH::Vec3 delta = end_position - out_body.Position;
	const float32 span = delta.Length();

	if (span < 1e-4f) {
		LogWarning(LC_PHYSICS, "Ragdoll: bone '{}' has no length, not creating a ragdoll", body_def.Bone.CStr());
		return false;
	}

	const JPH::Vec3 axis_world = delta / span;

	out_body.Length = span + body_def.ExtraLength * frame.Scale;
	out_body.Radius = body_def.Radius * frame.Scale;
	out_body.AxisLocal = rotation.Multiply3x3Transposed(axis_world);

	if (body_def.bFoot && body_def.FootWidth > 0.0f) {
		out_body.Shape = BuildFootBox(frame, body_def, rotation, out_body.Position, end_position, axis_world,
									  out_body.Radius);
	}
	else {
		out_body.Shape = BuildCapsule(out_body.AxisLocal, out_body.Length, out_body.Radius, frame.Scale);
	}

	JPH::RagdollSettings::Part& part = settings.mParts[body_index];
	ConfigurePart(part, out_body, body_def, frame.TotalMass);

	if (parent_body >= 0) {
		part.mToParent = BuildJoint(frame, body_def, parts[parent_body], out_body.Position, axis_world, out_body);
	}

	return true;
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

	SizedArray<ResolvedBone> parts;
	SizedArray<int32> body_of_bone;
	ResolveBones(skel, def, parts, body_of_bone);

	if (parts.Size < 2) {
		LogWarning(LC_PHYSICS, "Ragdoll: skeleton does not look like a humanoid, not creating a ragdoll");
		return false;
	}

	const float32 scale = RowAsVec3(object_world_matrix, 0).Length();

	BuildFrame frame { .Skel = skel,
					   .ObjectWorld = object_world_matrix,
					   .Scale = scale,
					   .TotalMass = def.TotalMass * scale * scale * scale,
					   .Up = GetUpFromGravity(gPhysics->pBackend->PhysicsSystem.GetGravity()) };
	frame.Forward = GetRestForward(frame, parts);

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
		const int32 parent_body = FindParentBody(skel, body_of_bone, bone);

		if (parent_body >= static_cast<int32>(i)) {
			LogWarning(LC_PHYSICS, "Ragdoll: body '{}' is listed before its parent body, not creating a ragdoll",
					   body_def.Bone.CStr());
			return false;
		}

		if (parent_body < 0 && i != 0) {
			LogWarning(LC_PHYSICS, "Ragdoll: bone '{}' has no ragdoll ancestor, not creating a ragdoll",
					   body_def.Bone.CStr());
			return false;
		}

		jolt_skeleton->AddJoint(std::string_view(body_def.Bone.CStr()), parent_body);

		BuiltBody body;

		if (!BuildBody(frame, parts, i, parent_body, *settings, body)) {
			return false;
		}

		joint_matrices.push_back(JPH::Mat44::sRotationTranslation(body.Orientation, body.Position));

		JPH::Vec3 parent_anchor = JPH::Vec3::sZero();
		JPH::Vec3 parent_hinge_local = JPH::Vec3::sZero();

		if (parent_body >= 0) {
			const JPH::RagdollSettings::Part& parent = settings->mParts[parent_body];

			parent_anchor = parent.mRotation.Conjugated() * JPH::Vec3(settings->mParts[i].mPosition - parent.mPosition);

			if (body.bHinge) {
				parent_hinge_local = parent.mRotation.Conjugated() * body.Hinge;
			}
		}

		const Mat4f& bone_model = skel.WorldTransforms[bone];

		mBodies.Insert(BodyInfo {

			.BoneScale = JPH::Vec3(RowAsVec3(bone_model, 0).Length(), RowAsVec3(bone_model, 1).Length(),
								   RowAsVec3(bone_model, 2).Length()),
			.ParentAnchor = parent_anchor,
			.LocalAxis = body.AxisLocal,
			.LocalCenter = body.Shape.CenterLocal,
			.BoxHalfExtent = body.Shape.BoxHalfExtent,
			.BoxRotation = body.Shape.BoxRotation,
			/* */
			.HingeLocal = body.Orientation.Conjugated() * body.Hinge,
			.ParentHingeLocal = parent_hinge_local,
			.RestPosition = body.Position,
			.RestOrientation = body.Orientation,
			/* */
			.ParentBody = parent_body,
			/* */
			.Part = body_def.Part,
			.Muscle = body_def.Muscle,
			.Length = body.Length,
			.Radius = body.Radius,
			.bBox = body.Shape.bBox,
			.Mass = body_def.MassFraction * frame.TotalMass,
			.BoneIndex = bone,
			.bHinge = body.bHinge,
			.bAnchor = body_def.bAnchor,
			.bFoot = body_def.bFoot,
		});
	}

	if (!settings->Stabilize()) {
		LogWarning(LC_PHYSICS, "Ragdoll: failed to stabilize the constraints");
	}

	RestoreTableMasses(*settings);

	settings->DisableParentChildCollisions(joint_matrices.data(), scSelfCollisionMargin * scale);

	if (!AddToPhysicsSystem(*settings)) {
		return false;
	}

	CreateRootAssist(JPH::Vec3(settings->mParts[0].mPosition));

	mbHasMotors = (mpRagdoll->GetConstraintCount() + 1 == mBodies.Size);

	InitBodyArrays(skel.JointCount);

	if (!mBalance.Init(*this)) {
		LogInfo(LC_PHYSICS, "Ragdoll: no two-legged support found, balance is unavailable");
	}

	mpSkeleton = skeleton;
	mWorldInverse = object_world_matrix.AffineInverse();

	const float32 skinning_error = WriteInitialPose();

	LogInfo(LC_PHYSICS, "Ragdoll: {} bodies, {:.3f} kg, rest pose skinning error {:.5f}", mBodies.Size, frame.TotalMass,
			skinning_error);

	return true;
}

void Ragdoll::RestoreTableMasses(JPH::RagdollSettings& settings)
{
	for (uint32 i = 0; i < mBodies.Size; i++) {
		JPH::MassProperties& mass_properties = settings.mParts[i].mMassPropertiesOverride;
		const float32 table_mass = mBodies[i].Mass;

		if (mass_properties.mMass > 0.0f) {
			mass_properties.mInertia *= table_mass / mass_properties.mMass;
			mass_properties.mInertia.SetColumn4(3, JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		mass_properties.mMass = table_mass;

		const JPH::Mat44& inertia = mass_properties.mInertia;
		mBodies[i].Inertia = (inertia(0, 0) + inertia(1, 1) + inertia(2, 2)) / 3.0f;
	}
}

bool Ragdoll::AddToPhysicsSystem(JPH::RagdollSettings& settings)
{
	static uint32 sNextGroupID = 0;

	JPH::PhysicsSystem& system = gPhysics->pBackend->PhysicsSystem;
	const uint32 serial = ++sNextGroupID;
	JPH::Ragdoll* ragdoll = settings.CreateRagdoll(serial, RagdollUserData::scTag | serial, &system);

	if (ragdoll == nullptr) {
		LogWarning(LC_PHYSICS, "Ragdoll: the physics system is out of bodies");
		return false;
	}

	mpRagdoll = ragdoll;
	mSerial = serial;
	mpRagdoll->AddToPhysicsSystem(JPH::EActivation::DontActivate);

	mpPhysicsSystem = &system;
	system.AddStepListener(this);

	return true;
}

void Ragdoll::CreateRootAssist(const JPH::Vec3 root_position)
{
	JPH::SixDOFConstraintSettings root_settings;
	root_settings.mSpace = JPH::EConstraintSpace::WorldSpace;
	root_settings.mNumVelocityStepsOverride = scSolverVelocitySteps;
	root_settings.mPosition1 = root_settings.mPosition2 = root_position;

	for (uint32 axis = 0; axis < JPH::SixDOFConstraint::EAxis::Num; axis++) {
		root_settings.MakeFreeAxis(static_cast<JPH::SixDOFConstraint::EAxis>(axis));
	}

	{
		JPH::BodyLockWrite lock(mpPhysicsSystem->GetBodyLockInterface(), mpRagdoll->GetBodyID(0));

		if (lock.Succeeded()) {
			mpRootAssist = static_cast<JPH::SixDOFConstraint*>(
				root_settings.Create(JPH::Body::sFixedToWorld, lock.GetBody()));
		}
	}

	if (mpRootAssist != nullptr) {
		mpRootAssist->SetEnabled(false);
		mpPhysicsSystem->AddConstraint(mpRootAssist);
		mRootAnchor = root_position;
	}
}

void Ragdoll::InitBodyArrays(uint32 joint_count)
{
	InitFilled(mTargets, mBodies.Size, BodyTarget {});
	InitFilled(mStates, mBodies.Size, BodyState {});
	InitFilled(mMotorOverrides, mBodies.Size, MotorOverride {});
	InitFilled(mLayers, mBodies.Size, BodyLayer {});

	mbHasTargets = false;

	InitFilled(mDrivenWorld, joint_count, Mat4f::scIdentity);
	InitFilled(mIsDriven, joint_count, static_cast<uint8>(0));

	for (const BodyInfo& body : mBodies) {
		mIsDriven[body.BoneIndex] = 1;
	}
}

float32 Ragdoll::WriteInitialPose()
{
	const Skeleton& skel = *mpSkeleton;
	const uint32 joint_count = skel.JointCount;

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

	return worst_error;
}

} // namespace fx::physics
