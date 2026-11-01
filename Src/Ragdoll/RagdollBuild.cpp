#include "Ragdoll.hpp"
#include "RagdollMath.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Collision/CollideShape.h>
#include <ThirdParty/Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <ThirdParty/Jolt/Physics/Collision/CollisionDispatch.h>
#include <ThirdParty/Jolt/Physics/Collision/GroupFilterTable.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <ThirdParty/Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <ThirdParty/Jolt/Skeleton/Skeleton.h>

#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
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
	const RDDefaultSegment* pDef = nullptr;
	uint32 StartBone;
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
		return GetBonePosition(part.EndBone) - GetBonePosition(part.StartBone);
	}
};

struct RDSegment
{
	JPH::ShapeRefC Shape;
	JPH::Vec3 CenterLocal = JPH::Vec3::sZero();
	bool bBox = false;

	/// The type of collider used in the shape. For most shapes, this should be a capsule. For shapes that must remain
	/// stable (such as feet), they should be a box.
	eRagdollShapeCollider ColliderType = eRagdollShapeCollider::Capsule;

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
	RDSegment Shape;
	bool bHinge = false;
	JPH::Vec3 Hinge = JPH::Vec3::sZero();
};

uint32 FindBoneIndex(const Skeleton& skeleton, const Name& name)
{
	for (uint32 i = 0; i < skeleton.JointCount; i++) {
		if (skeleton.BoneNames[i] == name) {
			return i;
		}
	}

	return scBoneNull;
}

void ResolveBones(const Skeleton& skel, const RDDefinition& def, SizedArray<ResolvedBone>& out_parts,
				  SizedArray<int32>& out_body_of_bone)
{
	out_body_of_bone.InitSize(skel.JointCount, -1);
	out_parts.InitCapacity(def.BodySegments.Size);

	for (const std::unique_ptr<RDDefaultSegment>& segment_def : def.BodySegments) {
		const uint32 start_bone_index = FindBoneIndex(skel, segment_def->StartBone);
		const uint32 end_bone_index = FindBoneIndex(skel, segment_def->EndBone);

		if (start_bone_index == scBoneNull || end_bone_index == scBoneNull) {
			const String missing_bone_name = (start_bone_index == scBoneNull) ? segment_def->StartBone.Get()
																			  : segment_def->EndBone.Get();

			LogError(LC_PHYSICS, "Skeleton is missing bone '{}', skipping segment", missing_bone_name);
			continue;
		}

		out_body_of_bone[start_bone_index] = static_cast<int32>(out_parts.Size);
		out_parts.Insert(
			ResolvedBone { .pDef = segment_def.get(), .StartBone = start_bone_index, .EndBone = end_bone_index });
	}
}

int32 FindParentBody(const Skeleton& skel, const SizedArray<int32>& body_of_bone, uint32 bone)
{
	for (uint32 ancestor = skel.ParentIndices[bone]; ancestor != scBoneNull; ancestor = skel.ParentIndices[ancestor]) {
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
		if (part.pDef->BodyPart == eRDBodyPart::Foot) {
			forward += frame.GetBoneSpan(part);
		}
	}

	return (forward - body_up * forward.Dot(body_up)).NormalizedOr(JPH::Vec3::sZero());
}

RDSegment BuildCapsule(const JPH::Vec3 axis_local, float32 length, float32 radius, float32 scale)
{
	const float32 half_height = std::max(length * 0.5f - radius, 0.01f * scale);

	RDSegment shape;
	shape.CenterLocal = axis_local * (length * 0.5f);

	JPH::ShapeRefC capsule = JPH::CapsuleShapeSettings(half_height, radius).Create().Get();
	shape.Shape = JPH::RotatedTranslatedShapeSettings(shape.CenterLocal,
													  JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis_local), capsule)
					  .Create()
					  .Get();

	return shape;
}

RDSegment BuildFootBox(const BuildFrame& frame, const RDFootSegment& body_def, const JPH::Mat44& rotation,
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

	RDSegment shape;
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

bool ApplyHingeLimits(JPH::SwingTwistConstraintSettings& joint, const BuildFrame& frame,
					  const RDDefaultSegment& body_def, const ResolvedBone& parent_part, const JPH::Vec3 axis_world,
					  JPH::Vec3& out_hinge)
{
	const float32 flex = MathUtil::DegToRad(body_def.FlexDegrees);
	const float32 slack = MathUtil::DegToRad(scHingeSlackDegrees);

	const JPH::Vec3 parent_axis = frame.GetBoneSpan(parent_part).NormalizedOr(axis_world);

	JPH::Vec3 flex_direction = -frame.Forward;

	if (body_def.Joint == eRDJoint::HingeForward) {
		flex_direction = frame.Forward;
	}
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

JPH::Ref<JPH::SwingTwistConstraintSettings> BuildJoint(const BuildFrame& frame, const RDDefaultSegment& body_def,
													   const ResolvedBone& parent_part, const JPH::Vec3 position,
													   const JPH::Vec3 axis_world, BuiltBody& out_body)
{
	JPH::Ref<JPH::SwingTwistConstraintSettings> joint = new JPH::SwingTwistConstraintSettings;

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

	if (body_def.Joint != eRDJoint::Cone) {
		out_body.bHinge = ApplyHingeLimits(*joint, frame, body_def, parent_part, axis_world, out_body.Hinge);
	}

	return joint;
}

void ConfigurePart(JPH::RagdollSettings::Part& part, const BuiltBody& body, const RDDefaultSegment& body_def,
				   float32 total_mass)
{
	part.SetShape(body.Shape.Shape);
	part.mPosition = body.Position;
	part.mRotation = body.Orientation;
	part.mMotionType = JPH::EMotionType::Dynamic;
	part.mObjectLayer = Ragdoll::GetModeLayer(eRagdollBodyMode::Limp);
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
	const RDDefaultSegment& body_def = *parts[body_index].pDef;

	const Mat4f bone_world = frame.GetBoneWorld(parts[body_index].StartBone);
	const JPH::Mat44 rotation = RotationFromMatrix(bone_world);

	out_body.Orientation = rotation.GetQuaternion().Normalized();
	out_body.Position = RowAsVec3(bone_world, 3);

	const JPH::Vec3 end_position = frame.GetBonePosition(parts[body_index].EndBone);
	const JPH::Vec3 delta = end_position - out_body.Position;
	const float32 span = delta.Length();

	if (span < 1e-4f) {
		LogError(LC_PHYSICS, "Ragdoll bone '{}' has no length", body_def.StartBone.Get().CStr());
		return false;
	}

	const JPH::Vec3 axis_world = delta / span;

	out_body.Length = span + body_def.ExtraLength * frame.Scale;
	out_body.Radius = body_def.Radius * frame.Scale;
	out_body.AxisLocal = rotation.Multiply3x3Transposed(axis_world);

	const RDFootSegment* foot_def = nullptr;

	if (body_def.BodyPart == eRDBodyPart::Foot) {
		foot_def = static_cast<const RDFootSegment*>(&body_def);
	}

	if (foot_def != nullptr && foot_def->FootWidth > 0.0f) {
		out_body.Shape = BuildFootBox(frame, *foot_def, rotation, out_body.Position, end_position, axis_world,
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

void EvaluateRestPose(Skeleton& skel)
{
	const AnimationPlayback* playback = skel.GetActivePlayback();

	if (playback != nullptr) {
		skel.EvaluatePose(playback->pAnimation, playback->Time);
	}
	else {
		skel.EvaluatePose(nullptr, 0.0f);
	}
}

RDSegmentInfo MakeSegmentInfo(const BuildFrame& frame, const ResolvedBone& part, const BuiltBody& body,
							  const JPH::RagdollSettings& settings, int32 parent_body)
{
	const RDDefaultSegment& body_def = *part.pDef;

	JPH::Vec3 parent_anchor = JPH::Vec3::sZero();
	JPH::Vec3 parent_hinge_local = JPH::Vec3::sZero();

	if (parent_body >= 0) {
		const JPH::RagdollSettings::Part& parent = settings.mParts[parent_body];

		parent_anchor = parent.mRotation.Conjugated() * JPH::Vec3(body.Position - parent.mPosition);

		if (body.bHinge) {
			parent_hinge_local = parent.mRotation.Conjugated() * body.Hinge;
		}
	}

	const Mat4f& bone_model = frame.Skel.WorldTransforms[part.StartBone];

	return RDSegmentInfo {
		.BoneScale = JPH::Vec3(RowAsVec3(bone_model, 0).Length(), RowAsVec3(bone_model, 1).Length(),
							   RowAsVec3(bone_model, 2).Length()),
		.ParentAnchor = parent_anchor,
		.LocalAxis = body.AxisLocal,
		.LocalCenter = body.Shape.CenterLocal,
		.BoxHalfExtent = body.Shape.BoxHalfExtent,
		.BoxRotation = body.Shape.BoxRotation,
		.HingeLocal = body.Orientation.Conjugated() * body.Hinge,
		.ParentHingeLocal = parent_hinge_local,
		.RestPosition = body.Position,
		.RestOrientation = body.Orientation,
		.ParentBody = parent_body,
		.Part = body_def.BodyPart,
		.Muscle = body_def.Muscle,
		.Length = body.Length,
		.Radius = body.Radius,
		.ColliderType = body.Shape.ColliderType,
		.Mass = body_def.MassFraction * frame.TotalMass,
		.BoneIndex = part.StartBone,
		.bHinge = body.bHinge,
		.bAnchor = body_def.bAnchor,
		.bFoot = body_def.BodyPart == eRDBodyPart::Foot,
	};
}

} // namespace

bool Ragdoll::Create(const Ref<Skeleton>& skeleton, const Mat4f& object_world_matrix, const RDDefinition& def)
{
	Destroy();

	if (!skeleton.IsValid() || skeleton->JointCount == 0 || gPhysics == nullptr || gPhysics->pBackend == nullptr) {
		return false;
	}

	Skeleton& skel = *skeleton;

	EvaluateRestPose(skel);

	SizedArray<ResolvedBone> parts;
	SizedArray<int32> body_of_bone;
	ResolveBones(skel, def, parts, body_of_bone);

	if (parts.Size < 2) {
		LogError(LC_PHYSICS, "Skeleton is not humanoid, skipping.");
		return false;
	}

	const float32 scale = RowAsVec3(object_world_matrix, 0).Length();
	const GravityFrame gravity_frame = GravityFrame::FromPhysics();

	BuildFrame frame { .Skel = skel,
					   .ObjectWorld = object_world_matrix,
					   .Scale = scale,
					   .TotalMass = def.TotalMass * scale * scale * scale,
					   .Up = gravity_frame.Up };
	frame.Forward = GetRestForward(frame, parts);

	JPH::Ref<JPH::Skeleton> jolt_skeleton = new JPH::Skeleton;
	JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings;

	settings->mSkeleton = jolt_skeleton;
	settings->mParts.resize(parts.Size);

	JPH::Array<JPH::Mat44> joint_matrices;
	joint_matrices.reserve(parts.Size);

	mSegments.InitCapacity(parts.Size);

	for (uint32 i = 0; i < parts.Size; i++) {
		const RDDefaultSegment& body_def = *parts[i].pDef;
		const int32 parent_body = FindParentBody(skel, body_of_bone, parts[i].StartBone);

		if (parent_body >= static_cast<int32>(i)) {
			LogWarning(LC_PHYSICS, "Ragdoll body '{}' is listed before its parent body",
					   body_def.StartBone.Get().CStr());
			return false;
		}

		if (parent_body < 0 && i != 0) {
			LogError(LC_PHYSICS, "Ragdoll bone '{}' has no ragdoll ancestor", body_def.StartBone.Get().CStr());
			return false;
		}

		jolt_skeleton->AddJoint(std::string_view(body_def.StartBone.Get().CStr()), parent_body);

		BuiltBody body;

		if (!BuildBody(frame, parts, i, parent_body, *settings, body)) {
			return false;
		}

		joint_matrices.push_back(JPH::Mat44::sRotationTranslation(body.Orientation, body.Position));

		mSegments.Insert(MakeSegmentInfo(frame, parts[i], body, *settings, parent_body));
	}

	for (const RDSegmentInfo& body : mSegments) {
		mTotalMass += body.Mass;
	}

	// Stabilize evens out masses along each chain so the solver converges, which changes the total mass. The def's
	// mass table is put back afterwards with the inertia scaled to match.
	if (!settings->Stabilize()) {
		LogWarning(LC_PHYSICS, "Ragdoll: failed to stabilize the constraints");
	}

	RestoreTableMasses(*settings);

	DisableSelfCollisions(*settings, joint_matrices.data(), scSelfCollisionMargin * scale);

	if (!AddToPhysicsSystem(*settings)) {
		return false;
	}

	CreateRootAssist(JPH::Vec3(settings->mParts[0].mPosition));

	mbHasMotors = (mpRagdoll->GetConstraintCount() + 1 == mSegments.Size);

	InitBodyArrays(skel.JointCount);
	BuildRig(gravity_frame);

	if (!mBalance.Init(*this)) {
		LogInfo(LC_PHYSICS, "Ragdoll: no two-legged support found, balance is unavailable");
	}

	mpSkeleton = skeleton;
	mWorldInverse = object_world_matrix.AffineInverse();

	WriteInitialPose();

	return true;
}

void Ragdoll::RestoreTableMasses(JPH::RagdollSettings& settings)
{
	for (uint32 i = 0; i < mSegments.Size; i++) {
		JPH::MassProperties& mass_properties = settings.mParts[i].mMassPropertiesOverride;
		const float32 table_mass = mSegments[i].Mass;

		if (mass_properties.mMass > 0.0f) {
			mass_properties.mInertia *= table_mass / mass_properties.mMass;
			mass_properties.mInertia.SetColumn4(3, JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		mass_properties.mMass = table_mass;

		const JPH::Mat44& inertia = mass_properties.mInertia;
		mSegments[i].Inertia = (inertia(0, 0) + inertia(1, 1) + inertia(2, 2)) / 3.0f;
	}
}

void Ragdoll::DisableSelfCollisions(JPH::RagdollSettings& settings, const JPH::Mat44* joint_matrices, float32 margin)
{
	const uint32 part_count = mSegments.Size;

	// Jolt's DisableParentChildCollisions keeps its table in a local reference and points the parts at it, so the table
	// is freed before the bodies are made. This table is kept on the ragdoll instead.
	mpSelfCollisionFilter = new JPH::GroupFilterTable(part_count);

	for (uint32 i = 0; i < part_count; i++) {
		if (mSegments[i].ParentBody >= 0) {
			mpSelfCollisionFilter->DisableCollision(i, static_cast<uint32>(mSegments[i].ParentBody));
		}

		for (uint32 j = i + 1; j < part_count; j++) {
			if (mSegments[i].bFoot && mSegments[j].bFoot) {
				mpSelfCollisionFilter->DisableCollision(i, j);
			}
		}
	}

	for (uint32 j1 = 0; j1 < part_count; j1++) {
		const JPH::RagdollSettings::Part& part1 = settings.mParts[j1];
		JPH::Vec3 scale1;
		const JPH::Mat44 com1 = joint_matrices[j1].PreTranslated(part1.GetShape()->GetCenterOfMass()).Decompose(scale1);

		for (uint32 j2 = j1 + 1; j2 < part_count; j2++) {
			const JPH::RagdollSettings::Part& part2 = settings.mParts[j2];

			if (!mpSelfCollisionFilter->IsCollisionEnabled(j1, j2) ||
				(!part1.HasMassProperties() && !part2.HasMassProperties())) {
				continue;
			}

			JPH::Vec3 scale2;
			const JPH::Mat44 com2 =
				joint_matrices[j2].PreTranslated(part2.GetShape()->GetCenterOfMass()).Decompose(scale2);

			JPH::CollideShapeSettings collide_settings;
			collide_settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
			collide_settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
			collide_settings.mMaxSeparationDistance = margin;

			JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;

			// Dispatch with the dynamic shape first, as Jolt needs it that way round for convex against mesh
			if (part1.HasMassProperties()) {
				JPH::CollisionDispatch::sCollideShapeVsShape(part1.GetShape(), part2.GetShape(), scale1, scale2, com1,
															 com2, JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(),
															 collide_settings, collector);
			}
			else {
				JPH::CollisionDispatch::sCollideShapeVsShape(part2.GetShape(), part1.GetShape(), scale2, scale1, com2,
															 com1, JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(),
															 collide_settings, collector);
			}

			if (collector.HadHit()) {
				mpSelfCollisionFilter->DisableCollision(j1, j2);
			}
		}
	}

	for (uint32 i = 0; i < part_count; i++) {
		JPH::RagdollSettings::Part& part = settings.mParts[i];
		part.mCollisionGroup.SetSubGroupID(i);
		part.mCollisionGroup.SetGroupFilter(mpSelfCollisionFilter);
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
	mSegmentTargets.InitSize(mSegments.Size, RDSegmentTarget {});
	mSegmentStates.InitSize(mSegments.Size, RDSegmentState {});
	mMotorOverrides.InitSize(mSegments.Size, MotorOverride {});
	mOverrideTargets.InitSize(mSegments.Size, JPH::Quat::sIdentity());
	mHadOverride.InitSize(mSegments.Size, uint8(0));
	mLayers.InitSize(mSegments.Size, BodyLayer {});
	mPreviousPoses.InitSize(mSegments.Size, BodyPose {});

	mbHasTargets = false;

	mDrivenWorld.InitSize(joint_count, Mat4f::scIdentity);
	mIsDriven.InitSize(joint_count, static_cast<uint8>(0));

	for (const RDSegmentInfo& body : mSegments) {
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
