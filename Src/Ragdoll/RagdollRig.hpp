#pragma once

#include "RagdollMath.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Quat.h>

#include <Core/Types.hpp>

namespace fx::physics {

struct RagdollRigArm
{
	uint32 Clavicle = 0;
	uint32 Upper = 0;
	uint32 Lower = 0;
	uint32 Hand = 0;
	float32 UpperLength = 0.0f;
	float32 LowerLength = 0.0f;
	float32 SideSign = 1.0f;
};

struct RagdollRigLeg
{
	static constexpr uint32 scSoleCorners = RagdollMath::scSoleCorners;

	uint32 Thigh = 0;
	uint32 Shin = 0;
	uint32 Foot = 0;
	float32 Upper = 0.0f;
	float32 Lower = 0.0f;
	float32 HingeSign = 1.0f;
	float32 SideSign = 1.0f;

	/// Hip joint in pelvis space
	JPH::Vec3 HipOffset = JPH::Vec3::sZero();

	/// Sole points in foot body space, only set for box feet
	JPH::Vec3 SoleCenter = JPH::Vec3::sZero();
	JPH::Vec3 SoleCorners[scSoleCorners];

	JPH::Quat RestFoot = JPH::Quat::sIdentity();
};

/**
 * @brief Named body indices and rest geometry of a humanoid ragdoll, built once by Ragdoll::Create and shared by the
 * balance controller and the behaviour layer.
 */
struct RagdollRig
{
	static constexpr uint32 scLimbCount = 2;
	static constexpr uint32 scMaxSpine = 4;

	RagdollRigArm Arms[scLimbCount];
	RagdollRigLeg Legs[scLimbCount];

	/// Chest first, walking down towards (but not including) the pelvis
	uint32 Spine[scMaxSpine];
	uint32 SpineCount = 0;
	uint32 Chest = 0;
	uint32 Neck = 0;

	JPH::Vec3 RestForward = JPH::Vec3::sAxisZ();
	float32 FootHalfWidth = 0.045f;
	float32 FootHalfLength = 0.1f;

	/// Two box feet on hinged shins hanging off the pelvis, and a chest
	bool bCanBalance = false;

	/// Two arms, two legs and a head on a spine, as the behaviour layer needs
	bool bValid = false;
};

} // namespace fx::physics
