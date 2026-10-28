#pragma once

#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Core/SizedArray.hpp>

namespace fx::physics {

enum class eRagdollJoint : uint8
{
	Cone,
	HingeForward,
	HingeBackward,
};

enum class eRagdollBodyPart : uint8
{
	Torso,
	Head,
	Arm,
	Leg,
};

struct RagdollMuscle
{
	float32 Stiffness = 10.0f;
	float32 Damping = 1.0f;
	float32 MaxTorque = 150.0f;
};

struct RagdollBodyDef
{
	String Bone;
	String EndBone;
	float32 ExtraLength = 0.0f;
	float32 Radius = 0.05f;
	float32 SwingDegrees = 0.0f;
	float32 TwistDegrees = 0.0f;
	float32 FlexDegrees = 0.0f;
	float32 MassFraction = 0.0f;
	float32 FootWidth = 0.0f;
	float32 HeelLength = 0.0f;
	float32 Friction = 0.8f;
	eRagdollJoint Joint = eRagdollJoint::Cone;
	eRagdollBodyPart Part = eRagdollBodyPart::Torso;
	bool bAnchor = false;
	bool bFoot = false;
	RagdollMuscle Muscle;
};

struct RagdollDef
{
	float32 TotalMass = 75.0f;
	SizedArray<RagdollBodyDef> Bodies;
};

bool LoadRagdollDef(const char* path, RagdollDef& out_def);
const RagdollDef* GetHumanoidRagdollDef();

} // namespace fx::physics
