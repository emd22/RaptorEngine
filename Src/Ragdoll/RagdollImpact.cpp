#include "RagdollImpact.hpp"

#include "Ragdoll.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Mat44.h>

#include <Engine.hpp>
#include <Math/MathUtil.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <algorithm>

namespace fx::physics {

namespace {

constexpr uint32 scLocalChainLength = 3;
constexpr float32 scLocalFalloff[scLocalChainLength] = { 1.0f, 0.6f, 0.3f };

/**
 * @brief How much of a blow on this body gets through to the rest of the figure. A limb swings with the blow and its
 * muscles only pass on part of it, so a shot to the forearm moves the arm and a shot to the chest moves the man.
 */
float32 GetKnockTransfer(const Ragdoll& ragdoll, int32 body)
{
	constexpr float32 scHead = 0.7f;
	constexpr float32 scClavicle = 0.6f;
	constexpr float32 scUpperArm = 0.35f;
	constexpr float32 scForearm = 0.2f;
	constexpr float32 scHand = 0.1f;
	constexpr float32 scThigh = 0.6f;
	constexpr float32 scShin = 0.3f;
	constexpr float32 scFoot = 0.2f;

	if (body < 0) {
		return 1.0f;
	}

	const uint32 index = static_cast<uint32>(body);
	const RagdollRig& rig = ragdoll.GetRig();

	if (index == rig.Neck) {
		return scHead;
	}

	for (const RagdollRigArm& arm : rig.Arms) {
		if (index == arm.Clavicle) {
			return scClavicle;
		}

		if (index == arm.Upper) {
			return scUpperArm;
		}

		if (index == arm.Lower) {
			return scForearm;
		}

		if (index == arm.Hand) {
			return scHand;
		}
	}

	for (const RagdollRigLeg& leg : rig.Legs) {
		if (index == leg.Thigh) {
			return scThigh;
		}

		if (index == leg.Shin) {
			return scShin;
		}

		if (index == leg.Foot) {
			return scFoot;
		}
	}

	return 1.0f;
}

/// Inertia of the figure about `pivot`, taken as one rigid body in its current pose
JPH::Mat44 GetFigureInertia(const Ragdoll& ragdoll, const JPH::Vec3 pivot)
{
	float32 xx = 0.0f;
	float32 yy = 0.0f;
	float32 zz = 0.0f;
	float32 xy = 0.0f;
	float32 xz = 0.0f;
	float32 yz = 0.0f;

	for (uint32 i = 0; i < ragdoll.GetSegmentCount(); i++) {
		const float32 mass = ragdoll.GetBodyMass(i);
		const float32 own = ragdoll.GetBodyInertia(i);
		const JPH::Vec3 arm = Vec3f::ToJolt(ragdoll.GetBodyPosition(i)) - pivot;

		const float32 x = arm.GetX();
		const float32 y = arm.GetY();
		const float32 z = arm.GetZ();

		xx += mass * (y * y + z * z) + own;
		yy += mass * (x * x + z * z) + own;
		zz += mass * (x * x + y * y) + own;
		xy -= mass * x * y;
		xz -= mass * x * z;
		yz -= mass * y * z;
	}

	return JPH::Mat44(JPH::Vec4(xx, xy, xz, 0.0f), JPH::Vec4(xy, yy, yz, 0.0f), JPH::Vec4(xz, yz, zz, 0.0f),
					  JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

} // namespace

Vec3f ApplyRagdollKnock(Ragdoll& ragdoll, const RagdollKnock& knock)
{
	// Fastest a blow can turn the figure about vertical (rad/s). It has little inertia that way, and arms held in
	// close would otherwise spin it like a top
	constexpr float32 scMaxKnockYawRate = 1.5f;
	// How far a sole can be off the ground and still count as a pivot (m)
	constexpr float32 scPivotClearance = 0.03f;

	const float32 total_mass = ragdoll.GetTotalMass();
	const uint32 body_count = ragdoll.GetSegmentCount();
	const JPH::Vec3 up = JPH::Vec3::sAxisY();

	if (total_mass <= 0.0f || body_count == 0) {
		return Vec3f::sZero;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	const RagdollRig& rig = ragdoll.GetRig();

	JPH::Vec3 center_of_mass = JPH::Vec3::sZero();
	JPH::Vec3 momentum = JPH::Vec3::sZero();

	for (uint32 i = 0; i < body_count; i++) {
		const float32 mass = ragdoll.GetBodyMass(i);

		center_of_mass += Vec3f::ToJolt(ragdoll.GetBodyPosition(i)) * mass;
		momentum += body_interface.GetLinearVelocity(ragdoll.GetBodyID(i)) * mass;
	}

	center_of_mass /= total_mass;

	JPH::Vec3 pivot = JPH::Vec3::sZero();
	bool planted[RagdollRig::scLimbCount] = {};
	uint32 planted_count = 0;

	if (rig.bCanBalance) {
		for (uint32 leg = 0; leg < RagdollRig::scLimbCount; leg++) {
			if (ragdoll.GetSoleClearance(leg) <= scPivotClearance) {
				planted[leg] = true;
				pivot += Vec3f::ToJolt(ragdoll.GetBodyOrigin(rig.Legs[leg].Foot));
				planted_count++;
			}
		}
	}

	const float32 transfer = GetKnockTransfer(ragdoll, knock.Body);
	JPH::Vec3 impulse = Vec3f::ToJolt(knock.Impulse) * transfer;
	JPH::Vec3 point = Vec3f::ToJolt(knock.Point);

	if (knock.Body < 0) {
		point = center_of_mass;
	}

	JPH::Vec3 linear = JPH::Vec3::sZero();

	if (planted_count > 0) {
		pivot /= static_cast<float32>(planted_count);
		// The floor takes anything pushing the figure down into it, and a pivot takes the rest of what isn't a turn
		impulse -= up * impulse.Dot(up);
	}
	else {
		pivot = center_of_mass;
		linear = impulse / total_mass;
	}

	JPH::Vec3 angular = GetFigureInertia(ragdoll, pivot).Inversed3x3().Multiply3x3((point - pivot).Cross(impulse));

	const float32 yaw = MathUtil::Clamp(angular.Dot(up), -scMaxKnockYawRate, scMaxKnockYawRate);

	angular = angular - up * angular.Dot(up) + up * yaw;

	const JPH::Vec3 raw_velocity = linear + angular.Cross(center_of_mass - pivot);
	const JPH::Vec3 flat_velocity = raw_velocity - up * raw_velocity.Dot(up);
	const float32 raw_speed = flat_velocity.Length();

	if (raw_speed <= 1e-5f) {
		return Vec3f::sZero;
	}

	const float32 wanted = MathUtil::Clamp(raw_speed, knock.MinSpeed * transfer, knock.MaxSpeed);

	// A figure already moving this way only gets topped up
	const JPH::Vec3 direction = flat_velocity / raw_speed;
	const float32 current = std::max(momentum.Dot(direction) / total_mass, 0.0f);
	const float32 added = std::max(wanted - current, wanted * knock.RepeatFraction);
	const float32 scale = added / raw_speed;

	linear *= scale;
	angular *= scale;

	for (uint32 i = 0; i < body_count; i++) {
		bool is_planted_foot = false;

		if (rig.bCanBalance) {
			for (uint32 leg = 0; leg < RagdollRig::scLimbCount; leg++) {
				is_planted_foot = is_planted_foot || (planted[leg] && i == rig.Legs[leg].Foot);
			}
		}

		if (is_planted_foot) {
			continue;
		}

		const JPH::BodyID body_id = ragdoll.GetBodyID(i);
		const JPH::Vec3 center = Vec3f::ToJolt(ragdoll.GetBodyPosition(i));

		body_interface.SetLinearAndAngularVelocity(
			body_id, body_interface.GetLinearVelocity(body_id) + linear + angular.Cross(center - pivot),
			body_interface.GetAngularVelocity(body_id) + angular);
		body_interface.ActivateBody(body_id);
	}

	return Vec3f::FromJolt(direction * added);
}

void ApplyRagdollLocalHit(Ragdoll& ragdoll, uint32 body, const Vec3f impulse, const Vec3f point)
{
	// Bullets carry little momentum for how hard they should look. This is how much harder the struck limb takes it
	constexpr float32 scLocalScale = 2.5f;
	constexpr float32 scMaxLocalSpeed = 2.0f;

	int32 chain[scLocalChainLength];
	uint32 chain_length = 0;
	float32 weighted_mass = 0.0f;

	for (int32 i = static_cast<int32>(body); i >= 0 && chain_length < scLocalChainLength;
		 i = ragdoll.GetParentBody(i)) {
		chain[chain_length] = i;
		weighted_mass += scLocalFalloff[chain_length] * ragdoll.GetBodyMass(static_cast<uint32>(i));
		chain_length++;
	}

	const float32 length = impulse.Length();

	if (weighted_mass <= 0.0f || length <= 1e-5f) {
		return;
	}

	const float32 speed = std::min(scLocalScale * length / weighted_mass, scMaxLocalSpeed);
	const Vec3f velocity = impulse * (speed / length);

	for (uint32 i = 0; i < chain_length; i++) {
		const uint32 index = static_cast<uint32>(chain[i]);
		const Vec3f chain_impulse = velocity * (scLocalFalloff[i] * ragdoll.GetBodyMass(index));

		// Only the struck body takes it off centre and spins
		if (i == 0) {
			ragdoll.AddImpulse(ragdoll.GetBodyID(index), chain_impulse, point);
		}
		else {
			ragdoll.AddImpulse(ragdoll.GetBodyID(index), chain_impulse, ragdoll.GetBodyPosition(index));
		}
	}
}

void LaunchRagdoll(Ragdoll& ragdoll, const Vec3f velocity)
{
	for (uint32 i = 0; i < ragdoll.GetSegmentCount(); i++) {
		ragdoll.AddImpulse(ragdoll.GetBodyID(i), velocity * ragdoll.GetBodyMass(i), ragdoll.GetBodyPosition(i));
	}
}

} // namespace fx::physics
