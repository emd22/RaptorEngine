#include "Ragdoll.hpp"
#include "RagdollMath.hpp"

#include <Core/Log.hpp>
#include <Math/MathUtil.hpp>

namespace fx::physics {

using namespace RagdollMath;

int32 Ragdoll::FindChildBody(uint32 parent, eRDBodyPart part) const
{
	for (uint32 i = parent + 1; i < mSegments.Size; i++) {
		if (mSegments[i].ParentBody == static_cast<int32>(parent) && mSegments[i].Part == part) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

bool Ragdoll::FindLimbChain(uint32 root, uint32& out_middle, uint32& out_end) const
{
	const eRDBodyPart part = mSegments[root].Part;
	const int32 middle = FindChildBody(root, part);

	if (middle < 0) {
		return false;
	}

	eRDBodyPart end_part = part;

	if (part == eRDBodyPart::Leg) {
		end_part = eRDBodyPart::Foot;
	}

	const int32 end = FindChildBody(static_cast<uint32>(middle), end_part);

	if (end < 0) {
		return false;
	}

	out_middle = static_cast<uint32>(middle);
	out_end = static_cast<uint32>(end);

	return true;
}

void Ragdoll::BuildRig(const GravityFrame& frame)
{
	mRig = RagdollRig {};

	if (mSegments.Size < 2) {
		return;
	}

	uint32 arm_count = 0;
	uint32 leg_count = 0;

	FindRigLimbs(arm_count, leg_count);

	const bool has_feet = (leg_count == RagdollRig::scLimbCount) && InitRigFeet(frame);
	const bool has_chest = (mRig.Neck != 0) && (mSegments[mRig.Neck].ParentBody >= 0);

	if (has_chest) {
		mRig.Chest = static_cast<uint32>(mSegments[mRig.Neck].ParentBody);

		for (int32 walk = static_cast<int32>(mRig.Chest); walk > 0 && mRig.SpineCount < RagdollRig::scMaxSpine;
			 walk = mSegments[walk].ParentBody) {
			mRig.Spine[mRig.SpineCount++] = static_cast<uint32>(walk);
		}
	}

	InitRigSides(frame, arm_count, leg_count);

	const bool has_limbs = (arm_count == RagdollRig::scLimbCount) && (leg_count == RagdollRig::scLimbCount);
	const bool legs_sided = (mRig.Legs[0].SideSign != mRig.Legs[1].SideSign);
	const bool arms_sided = (mRig.Arms[0].SideSign != mRig.Arms[1].SideSign);

	mRig.bCanBalance = has_feet && has_chest && legs_sided;
	mRig.bValid = has_limbs && has_chest && (mRig.Chest != 0) && legs_sided && arms_sided;

	if (!mRig.bValid) {
		LogWarning(LC_PHYSICS, "Ragdoll: expected two arms, two legs and a head on a spine ({} arms, {} legs)",
				   arm_count, leg_count);
	}
}

void Ragdoll::FindRigLimbs(uint32& out_arm_count, uint32& out_leg_count)
{
	for (uint32 i = 1; i < mSegments.Size; i++) {
		const RDSegmentInfo& body = mSegments[i];

		if (body.Part == eRDBodyPart::Head && mRig.Neck == 0) {
			mRig.Neck = i;
		}

		const bool is_limb = (body.Part == eRDBodyPart::Arm) || (body.Part == eRDBodyPart::Leg);

		// A limb starts at the first body of its part below a body of another part
		if (!is_limb || mSegments[body.ParentBody].Part == body.Part) {
			continue;
		}

		uint32 middle = 0;
		uint32 end = 0;

		if (!FindLimbChain(i, middle, end)) {
			continue;
		}

		if (body.Part == eRDBodyPart::Arm && out_arm_count < RagdollRig::scLimbCount) {
			RagdollRigArm& arm = mRig.Arms[out_arm_count++];

			arm.Clavicle = static_cast<uint32>(body.ParentBody);
			arm.Upper = i;
			arm.Lower = middle;
			arm.Hand = end;
			arm.UpperLength = (mSegments[middle].RestPosition - body.RestPosition).Length();
			arm.LowerLength = (mSegments[end].RestPosition - mSegments[middle].RestPosition).Length();
		}
		else if (body.Part == eRDBodyPart::Leg && out_leg_count < RagdollRig::scLimbCount) {
			RagdollRigLeg& leg = mRig.Legs[out_leg_count++];

			leg.Thigh = i;
			leg.Shin = middle;
			leg.Foot = end;
		}
	}
}

bool Ragdoll::InitRigFeet(const GravityFrame& frame)
{
	JPH::Vec3 forward = JPH::Vec3::sZero();

	for (RagdollRigLeg& leg : mRig.Legs) {
		const RDSegmentInfo& thigh = mSegments[leg.Thigh];
		const RDSegmentInfo& shin = mSegments[leg.Shin];
		const RDSegmentInfo& foot = mSegments[leg.Foot];

		if (!foot.bFoot || !shin.bHinge || thigh.ParentBody != 0) {
			return false;
		}

		leg.Upper = (shin.RestPosition - thigh.RestPosition).Length();
		leg.Lower = (foot.RestPosition - shin.RestPosition).Length();
		leg.HipOffset = thigh.ParentAnchor;
		leg.RestFoot = foot.RestOrientation;

		const JPH::Vec3 half = foot.BoxHalfExtent;

		for (uint32 corner = 0; corner < scSoleCorners; corner++) {
			leg.SoleCorners[corner] = foot.LocalCenter +
									  foot.BoxRotation * JPH::Vec3(scSoleCornerSigns[corner][0] * half.GetX(),
																   -half.GetY(),
																   scSoleCornerSigns[corner][1] * half.GetZ());
		}

		leg.SoleCenter = foot.LocalCenter + foot.BoxRotation * JPH::Vec3(0.0f, -half.GetY(), 0.0f);

		mRig.FootHalfWidth = half.GetX();
		mRig.FootHalfLength = half.GetZ();

		forward += foot.RestOrientation * foot.BoxRotation * JPH::Vec3::sAxisZ();
	}

	mRig.RestForward = frame.Horizontal(forward).NormalizedOr(JPH::Vec3::sAxisZ());

	return true;
}

void Ragdoll::InitRigSides(const GravityFrame& frame, uint32 arm_count, uint32 leg_count)
{
	const JPH::Vec3 side = frame.Up.Cross(mRig.RestForward);
	const JPH::Vec3 pelvis = mSegments[0].RestPosition;

	for (uint32 i = 0; i < arm_count; i++) {
		RagdollRigArm& arm = mRig.Arms[i];
		arm.SideSign = MathUtil::GetSign((mSegments[arm.Upper].RestPosition - pelvis).Dot(side));
	}

	for (uint32 i = 0; i < leg_count; i++) {
		RagdollRigLeg& leg = mRig.Legs[i];

		const RDSegmentInfo& thigh = mSegments[leg.Thigh];
		const RDSegmentInfo& shin = mSegments[leg.Shin];
		const RDSegmentInfo& foot = mSegments[leg.Foot];

		const JPH::Vec3 leg_direction = (foot.RestPosition - thigh.RestPosition).Normalized();
		const JPH::Vec3 rest_normal = leg_direction.Cross(mRig.RestForward);
		const JPH::Vec3 rest_hinge = shin.RestOrientation * shin.HingeLocal;

		leg.SideSign = MathUtil::GetSign((thigh.RestPosition - pelvis).Dot(side));
		leg.HingeSign = MathUtil::GetSign(rest_hinge.Dot(rest_normal));
	}
}

} // namespace fx::physics
