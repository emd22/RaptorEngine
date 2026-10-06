#include "WorldFile.hpp"

#include <Asset/AssetManager.hpp>
#include <Blockout.hpp>
#include <Engine.hpp>
#include <Physics/PhysicsManager.hpp>

namespace fx {


// void SceneFile::Save(const Scene& scene) {}

// void SceneFile::SaveObject(ConfigEntry& parent, const Object& object)
// {
//     ConfigEntry entry = ConfigEntry::Struct(object.Name);

//     ConfigEntry positions = ConfigEntry::Array("pos", ConfigEntry::ValueType::Float);
//     positions.AppendValue(object.mPosition);
//     entry.AddMember(std::move(positions));

//     ConfigEntry rotation = ConfigEntry::Array("rot", ConfigEntry::ValueType::Float);
//     rotation.AppendValue(object.mRotation);
//     entry.AddMember(std::move(rotation));

//     parent.AddMember(std::move(entry));
// }

// void WorldFile::Save(const String& path, const World& world)
// {
// ConfigFile info;

// info.Load("./info.prx");

// SizedArray<Object*> all_objects = gObjectManager->CollectObjects();

// ConfigEntry* objects_entry = info.AddEntry("Objects");

// for (const Object* object : all_objects) {
// 	ConfigEntry single_entry = ConfigEntry::Struct(object->Name.Get());

// 	single_entry.AddMember(ConfigEntry::Literal("Mesh", const TType &literal))

// 	objects_entry->AddMember(std::move(single_entry));
// }
// }


void WorldFile::Load(const std::string& path)
{
	ConfigFile info;

	gAssetManager->SetScenePath(path);

	info.Load(path + "/info.prx");

	// NOTE: this must not key off the world object count: blockout objects
	// attach independently of scene files and would falsely signal a reload.
	bool first_time = !gWorld->bIsPopulated;

	if (first_time) {
		ConfigEntry* meta = info.GetEntry(HashStr32("meta"));

		if (!meta) {
			LogError(LC_ASSET, "Project missing metadata!");
			return;
		}

		gWorld->Name = meta->GetMember(HashStr32("name"))->Get<const char*>();
	}

	// Lights are loaded from the blockout file (see Blockout::LoadLights), not the world file.

	ConfigEntry* collider_list = info.GetEntry(HashStr32("colliders"));
	if (collider_list) {
		for (const ConfigEntry& collider_entry : collider_list->Members) {
			if (first_time) {
				AddColliderFromEntry(path, collider_entry);
			}
		}
	}


	// Models are loaded from the blockout file as well (see Blockout::LoadModels). Blockouts saved before that keep
	// getting them from this file's `objects` entry until they are saved again.
	if (gWorld->pBlockout != nullptr) {
		if (gWorld->pBlockout->HasModelsEntry()) {
			gWorld->pBlockout->LinkModelColliders();
		}
		else {
			gWorld->pBlockout->LoadLegacyModels();
		}
	}

	if (first_time) {
		gWorld->bIsPopulated = true;
	}
}


void WorldFile::AddColliderFromEntry(const std::string& scene_path, const ConfigEntry& collider_entry)
{
	const std::string& collider_name = collider_entry.Name.Get();

	physics::eMotionType motion_type = physics::eMotionType::Static;
	physics::Body* phys = gPhysics->NewBody(collider_name);

	Vec3f position = collider_entry.GetMemberValue(HashStr32("pos"), Vec3f::sZero);
	Quat rotation = collider_entry.GetMemberValue(HashStr32("rot"), Quat::scIdentity);

	ConfigEntry* physics_type = collider_entry.GetMember(HashStr32("type"));
	if (physics_type && physics_type->Get<uint32>() == static_cast<uint32>(physics::eMotionType::Dynamic)) {
		motion_type = physics::eMotionType::Dynamic;
	}

	ConfigEntry* box = collider_entry.GetMember(HashStr32("box"));
	if (box != nullptr) {
		Vec3f size = box->GetMemberValue(HashStr32("size"), Vec3f::sOne);
		if (size.X <= 0.0f || size.Y <= 0.0f || size.Z <= 0.0f) {
			LogWarning(LC_PHYSICS, "Collider '{}' has invalid Size {} - falling back to 1,1,1", collider_name, size);
			size = Vec3f::sOne;
		}

		phys->CreatePrimitiveBody(physics::ePrimitiveType::Box, size, motion_type, physics::BodyProps {});
	}


	phys->Teleport(position, rotation);
}

} // namespace fx
