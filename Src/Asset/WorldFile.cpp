#include "WorldFile.hpp"

#include <Asset/AssetManager.hpp>
#include <Engine.hpp>
#include <Physics/PhysicsManager.hpp>

#include <memory>
#include <vector>

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


static_assert(static_cast<uint32>(physics::eMotionType::Dynamic) == 1);

namespace {

struct WorldFileDeleter
{
	void operator()(RxWorldFile* world) const { rx_world_file_free(world); }
};

using WorldFilePtr = std::unique_ptr<RxWorldFile, WorldFileDeleter>;

WorldFilePtr ReadWorldFile(const std::string& path)
{
	std::vector<uint8> bytes;

	if (!ReadConfigFileBytes(path, bytes)) {
		return nullptr;
	}

	const RxHost host = MakeConfigHost();
	const std::string constants_path = GetConfigConstantsPath();

	return WorldFilePtr(rx_world_file_parse(bytes.data(), bytes.size(), constants_path.c_str(), &host));
}

} // namespace

void WorldFile::Load(const std::string& path)
{
	gAssetManager->SetScenePath(path);

	WorldFilePtr info = ReadWorldFile(path + "/info.prx");

	// NOTE: this must not key off the world object count: blockout objects
	// attach independently of scene files and would falsely signal a reload.
	bool first_time = !gWorld->bIsPopulated;

	if (first_time) {
		if (info == nullptr || !rx_world_file_has_meta(info.get())) {
			LogError(LC_ASSET, "Project missing metadata!");
			return;
		}

		const char* name = rx_world_file_name(info.get());
		gWorld->Name = (name != nullptr) ? name : "";
	}

	if (info == nullptr) {
		return;
	}

	// Lights are loaded from the blockout file (see Blockout::LoadLights), not the world file.

	if (first_time) {
		for (uint32 i = 0; i < rx_world_file_collider_count(info.get()); i++) {
			AddColliderFromEntry(path, *rx_world_file_collider(info.get(), i));
		}
	}


	// Load objects

	for (uint32 i = 0; i < rx_world_file_object_count(info.get()); i++) {
		const RxWorldObject& object_entry = *rx_world_file_object(info.get(), i);

		if (first_time) {
			AddObjectFromEntry(path, object_entry);
		}
		else {
			Object* object = gObjectManager->FindObject(HashStr32(object_entry.name));
			if (object != nullptr) {
				ApplyPropertiesToObject(object, object_entry);
			}
			else {
				// New since the first load (or never found): add instead of
				// crashing on a null object.
				AddObjectFromEntry(path, object_entry);
			}
		}
	}

	if (first_time) {
		gWorld->bIsPopulated = true;
	}
}


void WorldFile::AddColliderFromEntry(const std::string& scene_path, const RxWorldCollider& collider_entry)
{
	const std::string collider_name(collider_entry.name, collider_entry.name_length);

	physics::eMotionType motion_type = physics::eMotionType::Static;
	physics::Body* phys = gPhysics->NewBody(collider_name);

	Vec3f position(collider_entry.position);
	Quat rotation(collider_entry.rotation);

	if (collider_entry.dynamic) {
		motion_type = physics::eMotionType::Dynamic;
	}

	if (collider_entry.has_box) {
		Vec3f size(collider_entry.box_size);
		if (size.X <= 0.0f || size.Y <= 0.0f || size.Z <= 0.0f) {
			LogWarning(LC_PHYSICS, "Collider '{}' has invalid Size {} - falling back to 1,1,1", collider_name, size);
			size = Vec3f::sOne;
		}

		phys->CreatePrimitiveBody(physics::ePrimitiveType::Box, size, motion_type, physics::BodyProps {});
	}


	phys->Teleport(position, rotation);
}

void WorldFile::AddObjectFromEntry(const std::string& scene_path, const RxWorldObject& object_entry)
{
	if (object_entry.mesh == nullptr) {
		LogError(LC_ASSET, "Object '{}' has no mesh, skipping it", object_entry.name);
		return;
	}

	LoadObjectOptions load_options {};

	String path = String::Fmt("{}/Models{}", (scene_path), object_entry.mesh);
	AssetTicket ticket = gAssetManager->LoadObject(object_entry.name, path.CStr());

	Object* object = static_cast<Object*>(ticket.Get());


	ApplyPropertiesToObject(object, object_entry);

	gWorld->Attach(ticket);
}


void WorldFile::ApplyPropertiesToObject(Object* object, const RxWorldObject& object_entry)
{
	if (object_entry.has_shadows) {
		object->SetShadowCaster(object_entry.shadows != 0);
	}

	// Transforms

	object->SetPosition(object_entry.has_position ? Vec3f(object_entry.position) : object->mPosition);
	object->SetRotation(object_entry.has_rotation ? Quat(object_entry.rotation) : object->mRotation);
	object->SetScale(object_entry.has_scale ? object_entry.scale : object->mScale);
	object->MarkTransformOutOfDate();

	// Render options

	if (object_entry.has_layer) {
		if (object_entry.layer == static_cast<int64>(eObjectLayer::PlayerLayer)) {
			object->SetObjectLayer(eObjectLayer::PlayerLayer);
		}
	}

	object->SetUnlit(object_entry.unlit != 0);

	if (object_entry.no_cull) {
		object->SetCullable(false);
	}


	physics::BodyProps physics_properties {};

	// Physics

	if (object_entry.collider != nullptr) {
		physics::Body* phys_object = gPhysics->FindBody(HashStr32(object_entry.collider));
		if (phys_object != nullptr) {
			object->AttachCollider(phys_object);
			object->SetPhysicsEnabled(true);
		}
		else {
			object->SetPhysicsID(physics::BodyID::scNull);
		}
	}
}

} // namespace fx
