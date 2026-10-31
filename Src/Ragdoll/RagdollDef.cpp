#include "RagdollDef.hpp"

#include <Asset/ConfigFile.hpp>
#include <Core/Hash.hpp>
#include <Core/Log.hpp>
#include <cmath>
#include <cstring>
#include <utility>

namespace fx::physics {

namespace {

constexpr const char* scHumanoidPath = "RaptorData/Data/Demo/Models/ragdoll2/humanoid.ragdoll";
constexpr float32 scDefaultTotalMass = 75.0f;
constexpr float32 scMassToleranceKg = 2.0f;

eRDJoint ParseJoint(const char* name, bool& out_ok)
{
	out_ok = true;

	if (std::strcmp(name, "cone") == 0) {
		return eRDJoint::Cone;
	}
	if (std::strcmp(name, "hinge_forward") == 0) {
		return eRDJoint::HingeForward;
	}
	if (std::strcmp(name, "hinge_backward") == 0) {
		return eRDJoint::HingeBackward;
	}

	out_ok = false;
	return eRDJoint::Cone;
}

eRDBodyPart ParseBodyPart(const char* name, bool& out_ok)
{
	out_ok = true;

	if (std::strcmp(name, "torso") == 0) {
		return eRDBodyPart::Torso;
	}
	if (std::strcmp(name, "head") == 0) {
		return eRDBodyPart::Head;
	}
	if (std::strcmp(name, "arm") == 0) {
		return eRDBodyPart::Arm;
	}
	if (std::strcmp(name, "leg") == 0) {
		return eRDBodyPart::Leg;
	}

	out_ok = false;
	return eRDBodyPart::Torso;
}

std::unique_ptr<RDDefaultSegment> ReadSegment(const ConfigEntry& entry, uint32 index)
{
	const char* bone = entry.GetMemberValue<const char*>(HashStr32("bone"), nullptr);
	const char* end_bone = entry.GetMemberValue<const char*>(HashStr32("end"), nullptr);

	if (bone == nullptr || end_bone == nullptr) {
		LogError(LC_PHYSICS, "Ragdoll body {} needs 'bone' and 'end'", index);
		return nullptr;
	}

	const char* joint_name = entry.GetMemberValue<const char*>(HashStr32("joint"), "cone");
	bool joint_ok = false;
	const eRDJoint joint = ParseJoint(joint_name, joint_ok);

	if (!joint_ok) {
		LogError(LC_PHYSICS, "Ragdoll body {} ('{}') has unknown joint '{}'", index, bone, joint_name);
		return nullptr;
	}

	const char* part_name = entry.GetMemberValue<const char*>(HashStr32("part"), "torso");
	bool part_ok = false;
	const eRDBodyPart part = ParseBodyPart(part_name, part_ok);

	if (!part_ok) {
		LogError(LC_PHYSICS, "Ragdoll body {} ('{}') has unknown part '{}'", index, bone, part_name);
		return nullptr;
	}

	const bool bIsFoot = entry.GetMemberValue<int32>(HashStr32("foot"), 0) != 0;

	std::unique_ptr<RDDefaultSegment> segment;

	if (bIsFoot) {
		std::unique_ptr<RDFootSegment> foot = std::make_unique<RDFootSegment>();
		foot->FootWidth = entry.GetMemberValue<float32>(HashStr32("width"), 0.0f);
		foot->HeelLength = entry.GetMemberValue<float32>(HashStr32("heel"), 0.0f);
		segment = std::move(foot);
	}
	else {
		segment = std::make_unique<RDDefaultSegment>();
	}

	RDDefaultSegment& out = *segment;

	out.StartBone = bone;
	out.EndBone = end_bone;
	out.ExtraLength = entry.GetMemberValue<float32>(HashStr32("extra_length"), 0.0f);
	out.Radius = entry.GetMemberValue<float32>(HashStr32("radius"), out.Radius);
	out.SwingDegrees = entry.GetMemberValue<float32>(HashStr32("swing"), 0.0f);
	out.TwistDegrees = entry.GetMemberValue<float32>(HashStr32("twist"), 0.0f);
	out.FlexDegrees = entry.GetMemberValue<float32>(HashStr32("flex"), 0.0f);
	out.MassFraction = entry.GetMemberValue<float32>(HashStr32("mass_fraction"), 0.0f);
	out.Joint = joint;
	out.bAnchor = entry.GetMemberValue<int32>(HashStr32("anchor"), 0) != 0;
	out.Friction = entry.GetMemberValue<float32>(HashStr32("friction"), out.Friction);
	out.Muscle.Stiffness = entry.GetMemberValue<float32>(HashStr32("stiffness"), out.Muscle.Stiffness);
	out.Muscle.Damping = entry.GetMemberValue<float32>(HashStr32("damping"), out.Muscle.Damping);
	out.Muscle.MaxTorque = entry.GetMemberValue<float32>(HashStr32("max_torque"), out.Muscle.MaxTorque);

	// A foot is always the Foot body part, whatever its part key says, since the constructor sets it
	if (!bIsFoot) {
		out.BodyPart = part;
	}

	return segment;
}

} // namespace

bool RDLoadDefinition(const char* path, RDDefinition& out_def)
{
	ConfigFile config;
	config.Load(path);

	if (config.HasErrors()) {
		LogError(LC_PHYSICS, "Could not parse ragdoll config '{}'", path);
		return false;
	}

	const ConfigEntry* bodies = config.GetEntry(HashStr32("bodies"));

	if (bodies == nullptr || !bodies->IsObjectArray() || !bodies->Members.IsInited()) {
		LogError(LC_PHYSICS, "Ragdoll config '{}' needs a 'bodies' array of tables", path);
		return false;
	}

	RDDefinition def;
	def.TotalMass = config.GetEntryValue<float32>(HashStr32("total_mass"), scDefaultTotalMass);

	def.BodySegments.InitCapacity(bodies->Members.Size());

	float32 fraction_sum = 0.0f;

	for (const ConfigEntry& entry : bodies->Members) {
		std::unique_ptr<RDDefaultSegment> segment = ReadSegment(entry, static_cast<uint32>(def.BodySegments.Size));

		if (segment == nullptr) {
			return false;
		}

		fraction_sum += segment->MassFraction;
		def.BodySegments.Insert(std::move(segment));
	}

	if (def.BodySegments.IsEmpty()) {
		LogError(LC_PHYSICS, "Ragdoll config '{}' has no bodies", path);
		return false;
	}

	const float32 table_mass = fraction_sum * def.TotalMass;

	if (std::abs(table_mass - def.TotalMass) > scMassToleranceKg) {
		LogError(LC_PHYSICS, "Ragdoll config '{}': mass table sums to {:.3f} kg, expected {:.1f} kg", path, table_mass,
				 def.TotalMass);
		return false;
	}

	LogInfo(LC_PHYSICS, "Ragdoll config '{}': {} bodies, mass table {:.3f} kg", path, def.BodySegments.Size,
			table_mass);

	out_def = std::move(def);
	return true;
}

const RDDefinition* RDGetHumanoidDefinition()
{
	static RDDefinition sDef;
	static const bool sbLoaded = RDLoadDefinition(scHumanoidPath, sDef);

	if (!sbLoaded) {
		return nullptr;
	}

	return &sDef;
}

} // namespace fx::physics
