#include "WorldFile.hpp"

#include <Asset/AssetManager.hpp>
#include <CVar.hpp>
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

	// Load sun
	Ref<LightDirectional> sun = gWorld->GetDirectionalLight();
	if (!sun.IsValid()) {
		sun = Ref<LightDirectional>::New();
		gWorld->Attach(sun);
	}

	ConfigEntry* sun_entry = info.GetEntry(HashStr32("sun"));

	// A scene without a sun block, or with `enabled = $False` in it, has no sunlight. RaptorGame applies the cvar every
	// frame, so the sun can also be toggled from the console with `$b_sun_enabled 0`.
	const bool sun_enabled = (sun_entry != nullptr) &&
							 (sun_entry->GetMemberValue<int64>(HashStr32("enabled"), 1) != 0);

	sun->bEnabled = sun_enabled;
	gCVars->Set("b_sun_enabled", sun_enabled);

	if (sun_entry) {
		sun->SetPosition(sun_entry->GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero));

		sun->Color = sun_entry->GetMemberValue<Color>(HashStr32("color"), Color::FromRGBA(100, 100, 100, 4));
		sun->AmbientColor = sun_entry->GetMemberValue<Color>(HashStr32("ambient"), Color::FromRGBA(100, 100, 100, 1));
	}

	// Load point and spot lights

	ConfigEntry* light_list = info.GetEntry(HashStr32("lights"));
	if (light_list) {
		for (const ConfigEntry& light_entry : light_list->Members) {
			AddOrUpdateLightFromEntry(light_entry);
		}
	}

	ConfigEntry* collider_list = info.GetEntry(HashStr32("colliders"));
	if (collider_list) {
		for (const ConfigEntry& collider_entry : collider_list->Members) {
			if (first_time) {
				AddColliderFromEntry(path, collider_entry);
			}
		}
	}


	// Load objects

	ConfigEntry* object_list = info.GetEntry(HashStr32("objects"));

	for (const ConfigEntry& object_entry : object_list->Members) {
		if (first_time) {
			AddObjectFromEntry(path, object_entry);
		}
		else {
			Object* object = gObjectManager->FindObject(object_entry.Name.GetHash());
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

/// Values of the CLight constants in Config/Internal/Constants.conf
enum class eWorldFileLightType : int64
{
	Point = 0,
	Spot = 1,
};

void WorldFile::AddOrUpdateLightFromEntry(const ConfigEntry& light_entry)
{
	const int64 type_value = light_entry.GetMemberValue<int64>(HashStr32("type"),
															  static_cast<int64>(eWorldFileLightType::Point));

	eLightType light_type;

	switch (static_cast<eWorldFileLightType>(type_value)) {
	case eWorldFileLightType::Point:
		light_type = eLightType::Point;
		break;
	case eWorldFileLightType::Spot:
		light_type = eLightType::Spot;
		break;
	default:
		LogError(LC_ASSET, "Light '{}' has unknown type {}", light_entry.Name.Get(), type_value);
		return;
	}

	// Lights are matched by name so a hot reload updates them in place
	Ref<LightBase> light = gWorld->FindLight(light_entry.Name.GetHash());

	if (light.IsValid() && light->Type != light_type) {
		LogWarning(LC_ASSET, "Light '{}' changed type, restart to apply", light_entry.Name.Get());
		return;
	}

	if (!light.IsValid()) {
		if (light_type == eLightType::Spot) {
			light = Ref<LightSpot>::New();
		}
		else {
			light = Ref<LightPoint>::New();
		}

		light->Name = light_entry.Name.Get();
		gWorld->Attach(light);
	}

	light->SetPosition(light_entry.GetMemberValue(HashStr32("pos"), light->GetPosition()));
	light->Color = light_entry.GetMemberValue(HashStr32("color"), Color::FromRGBA(255, 255, 255, 8));
	light->SetRadius(light_entry.GetMemberValue<float32>(HashStr32("radius"), 5.0f));

	if (light_type == eLightType::Spot) {
		Ref<LightSpot> spot(light);

		// Aim with either a direction or a rotation (the cone points along +Z)
		ConfigEntry* direction = light_entry.GetMember(HashStr32("dir"));
		if (direction != nullptr) {
			spot->SetDirection(direction->GetValue<Vec3f>());
		}
		else {
			spot->SetRotation(light_entry.GetMemberValue(HashStr32("rot"), spot->mRotation));
		}

		// Cone half-angles, in degrees
		const float32 inner_angle = light_entry.GetMemberValue<float32>(HashStr32("inner"), 20.0f);
		const float32 outer_angle = light_entry.GetMemberValue<float32>(HashStr32("outer"), 30.0f);

		spot->SetConeAngles(MathUtil::DegreesToRadians(inner_angle), MathUtil::DegreesToRadians(outer_angle));

		// Spot lights bake a shadow map into the shadow atlas unless `shadows = $False`
		spot->bCastShadows = (light_entry.GetMemberValue<int64>(HashStr32("shadows"), 1) != 0);
	}
}

void WorldFile::AddObjectFromEntry(const std::string& scene_path, const ConfigEntry& object_entry)
{
	const char* mesh_path = object_entry.GetMember(HashStr32("mesh"))->Get<const char*>();

	LoadObjectOptions load_options {};

	String path = String::Fmt("{}/Models{}", (scene_path), mesh_path);
	AssetTicket ticket = gAssetManager->LoadObject(object_entry.Name.Get(), path.CStr());

	Object* object = static_cast<Object*>(ticket.Get());


	ApplyPropertiesToObject(object, object_entry);

	gWorld->Attach(ticket);
}


void WorldFile::ApplyPropertiesToObject(Object* object, const ConfigEntry& object_entry)
{
	ConfigEntry* shadow_caster = object_entry.GetMember(HashStr32("shadows"));
	if (shadow_caster != nullptr) {
		object->SetShadowCaster(static_cast<bool>(shadow_caster->Get<int64>()));
	}

	// Transforms

	object->SetPosition(object_entry.GetMemberValue(HashStr32("pos"), object->mPosition));
	object->SetRotation(object_entry.GetMemberValue(HashStr32("rot"), object->mRotation));
	object->SetScale(object_entry.GetMemberValue(HashStr32("scale"), object->mScale));
	object->MarkTransformOutOfDate();

	// Render options

	ConfigEntry* layer = object_entry.GetMember(HashStr32("layer"));
	if (layer != nullptr) {
		int64 layer_value = layer->Get<int64>();

		if (layer_value == static_cast<int64>(eObjectLayer::PlayerLayer)) {
			object->SetObjectLayer(eObjectLayer::PlayerLayer);
		}
	}

	object->SetUnlit(static_cast<bool>(object_entry.GetMemberValue(HashStr32("unlit"), 0)));

	ConfigEntry* nocull = object_entry.GetMember(HashStr32("nocull"));
	if (nocull != nullptr) {
		object->SetCullable(false);
	}


	physics::BodyProps physics_properties {};

	// Physics

	ConfigEntry* collider_ref = object_entry.GetMember(HashStr32("collider"));
	if (collider_ref != nullptr) {
		physics::Body* phys_object = gPhysics->FindBody(HashStr32(collider_ref->Get<const char*>()));
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
