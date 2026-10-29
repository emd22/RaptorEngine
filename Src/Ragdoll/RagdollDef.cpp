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

eRagdollJoint ParseJoint(const char* name, bool& out_ok)
{
	out_ok = true;

	if (std::strcmp(name, "cone") == 0) {
		return eRagdollJoint::Cone;
	}
	if (std::strcmp(name, "hinge_forward") == 0) {
		return eRagdollJoint::HingeForward;
	}
	if (std::strcmp(name, "hinge_backward") == 0) {
		return eRagdollJoint::HingeBackward;
	}

	out_ok = false;
	return eRagdollJoint::Cone;
}

eRagdollBodyPart ParseBodyPart(const char* name, bool& out_ok)
{
	out_ok = true;

	if (std::strcmp(name, "torso") == 0) {
		return eRagdollBodyPart::Torso;
	}
	if (std::strcmp(name, "head") == 0) {
		return eRagdollBodyPart::Head;
	}
	if (std::strcmp(name, "arm") == 0) {
		return eRagdollBodyPart::Arm;
	}
	if (std::strcmp(name, "leg") == 0) {
		return eRagdollBodyPart::Leg;
	}

	out_ok = false;
	return eRagdollBodyPart::Torso;
}

bool ReadBody(const ConfigEntry& entry, uint32 index, RagdollBodyDef& out_body)
{
	const char* bone = entry.GetMemberValue<const char*>(HashStr32("bone"), nullptr);
	const char* end_bone = entry.GetMemberValue<const char*>(HashStr32("end"), nullptr);

	if (bone == nullptr || end_bone == nullptr) {
		LogError(LC_PHYSICS, "Ragdoll body {} needs 'bone' and 'end'", index);
		return false;
	}

	const char* joint_name = entry.GetMemberValue<const char*>(HashStr32("joint"), "cone");
	bool joint_ok = false;
	const eRagdollJoint joint = ParseJoint(joint_name, joint_ok);

	if (!joint_ok) {
		LogError(LC_PHYSICS, "Ragdoll body {} ('{}') has unknown joint '{}'", index, bone, joint_name);
		return false;
	}

	const char* part_name = entry.GetMemberValue<const char*>(HashStr32("part"), "torso");
	bool part_ok = false;
	const eRagdollBodyPart part = ParseBodyPart(part_name, part_ok);

	if (!part_ok) {
		LogError(LC_PHYSICS, "Ragdoll body {} ('{}') has unknown part '{}'", index, bone, part_name);
		return false;
	}

	out_body.Bone = bone;
	out_body.EndBone = end_bone;
	out_body.ExtraLength = entry.GetMemberValue<float32>(HashStr32("extra_length"), 0.0f);
	out_body.Radius = entry.GetMemberValue<float32>(HashStr32("radius"), out_body.Radius);
	out_body.SwingDegrees = entry.GetMemberValue<float32>(HashStr32("swing"), 0.0f);
	out_body.TwistDegrees = entry.GetMemberValue<float32>(HashStr32("twist"), 0.0f);
	out_body.FlexDegrees = entry.GetMemberValue<float32>(HashStr32("flex"), 0.0f);
	out_body.MassFraction = entry.GetMemberValue<float32>(HashStr32("mass_fraction"), 0.0f);
	out_body.Joint = joint;
	out_body.Part = part;
	out_body.bAnchor = entry.GetMemberValue<int32>(HashStr32("anchor"), 0) != 0;
	out_body.bFoot = entry.GetMemberValue<int32>(HashStr32("foot"), 0) != 0;
	out_body.FootWidth = entry.GetMemberValue<float32>(HashStr32("width"), 0.0f);
	out_body.HeelLength = entry.GetMemberValue<float32>(HashStr32("heel"), 0.0f);
	out_body.Friction = entry.GetMemberValue<float32>(HashStr32("friction"), out_body.Friction);
	out_body.Muscle.Stiffness = entry.GetMemberValue<float32>(HashStr32("stiffness"), out_body.Muscle.Stiffness);
	out_body.Muscle.Damping = entry.GetMemberValue<float32>(HashStr32("damping"), out_body.Muscle.Damping);
	out_body.Muscle.MaxTorque = entry.GetMemberValue<float32>(HashStr32("max_torque"), out_body.Muscle.MaxTorque);

	return true;
}

} // namespace

bool LoadRagdollDef(const char* path, RagdollDef& out_def)
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

	RagdollDef def;
	def.TotalMass = config.GetEntryValue<float32>(HashStr32("total_mass"), scDefaultTotalMass);

	def.Bodies.InitCapacity(bodies->Members.Size());

	float32 fraction_sum = 0.0f;

	for (const ConfigEntry& entry : bodies->Members) {
		RagdollBodyDef body;

		if (!ReadBody(entry, static_cast<uint32>(def.Bodies.Size), body)) {
			return false;
		}

		fraction_sum += body.MassFraction;
		def.Bodies.Insert(body);
	}

	if (def.Bodies.IsEmpty()) {
		LogError(LC_PHYSICS, "Ragdoll config '{}' has no bodies", path);
		return false;
	}

	const float32 table_mass = fraction_sum * def.TotalMass;

	if (std::abs(table_mass - def.TotalMass) > scMassToleranceKg) {
		LogError(LC_PHYSICS, "Ragdoll config '{}': mass table sums to {:.3f} kg, expected {:.1f} kg", path, table_mass,
				 def.TotalMass);
		return false;
	}

	LogInfo(LC_PHYSICS, "Ragdoll config '{}': {} bodies, mass table {:.3f} kg", path, def.Bodies.Size, table_mass);

	out_def = std::move(def);
	return true;
}

const RagdollDef* GetHumanoidRagdollDef()
{
	static RagdollDef sDef;
	static const bool sbLoaded = LoadRagdollDef(scHumanoidPath, sDef);

	if (!sbLoaded) {
		return nullptr;
	}

	return &sDef;
}

} // namespace fx::physics
