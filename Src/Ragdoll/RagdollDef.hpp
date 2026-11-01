#pragma once

#include <Core/Name.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <memory>

namespace fx::physics {

enum class eRDJoint : uint8
{
	Cone,
	HingeForward,
	HingeBackward,
};

enum class eRDBodyPart : uint8
{
	Torso,
	Head,
	Arm,
	Leg,
	Foot,
};

struct RDMuscle
{
	float32 Stiffness = 10.0f;
	float32 Damping = 1.0f;
	float32 MaxTorque = 150.0f;
};


/////////////////////////////////////
// Segment Types
/////////////////////////////////////


struct RDDefaultSegment
{
public:
	RDDefaultSegment() = default;
	RDDefaultSegment(eRDBodyPart body_part) : BodyPart(body_part) {}

	virtual ~RDDefaultSegment() = default;

public:
	Name StartBone;
	Name EndBone;

	float32 Radius = 0.05f;
	float32 SwingDegrees = 0.0f;
	float32 TwistDegrees = 0.0f;
	float32 FlexDegrees = 0.0f;

	float32 MassFraction = 0.0f;
	float32 Friction = 0.8f;

	/// Additional length added to the body segment (fingers, toes, neck, etc.)
	float32 ExtraLength = 0.0f;

	eRDJoint Joint = eRDJoint::Cone;
	eRDBodyPart BodyPart = eRDBodyPart::Torso;

	bool bAnchor = false;

	RDMuscle Muscle;
};

struct RDFootSegment : public RDDefaultSegment
{
public:
	RDFootSegment(float32 foot_width, float32 heel_length) : FootWidth(foot_width), HeelLength(heel_length)
	{
		this->BodyPart = eRDBodyPart::Foot;
	}

public:
	float32 FootWidth = 0.0f;
	float32 HeelLength = 0.0f;
};


/////////////////////////////////////
// Ragdoll Definitions
/////////////////////////////////////


struct RDDefinition
{
	float32 TotalMass = 75.0f;
	SizedArray<std::unique_ptr<RDDefaultSegment>> BodySegments;
};

bool RDLoadDefinition(const char* path, RDDefinition& out_def);
const RDDefinition* RDGetHumanoidDefinition();

} // namespace fx::physics
