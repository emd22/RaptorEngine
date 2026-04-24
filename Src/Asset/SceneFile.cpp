#include "SceneFile.hpp"

#include <Asset/AxManager.hpp>
#include <Engine.hpp>

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


void SceneFile::Load(const std::string& path, Scene& scene)
{
    ConfigFile info;

    info.Load(path + "/info.prx");

    bool first_time = (scene.GetAllObjects().Size() == 0);

    if (first_time) {
        ConfigEntry* meta = info.GetEntry(HashStr32("Meta"));

        if (!meta) {
            LogError("Project missing metadata!");
            return;
        }

        scene.Name = meta->GetMember(HashStr32("Name"))->Get<const char*>();
    }

    // Load sun

    Ref<LightDirectional> sun = scene.GetDirectionalLight();
    if (!sun.IsValid()) {
        sun = Ref<LightDirectional>::New();
        scene.Attach(sun);
    }

    ConfigEntry* sun_entry = info.GetEntry(HashStr32("Sun"));

    if (sun_entry) {
        sun->SetPosition(sun_entry->GetMemberValue<Vec3f>(HashStr32("Pos"), Vec3f::sZero));

        sun->Color = sun_entry->GetMemberValue<Color>(HashStr32("Color"), Color::FromRGBA(100, 100, 100, 4));
        sun->AmbientColor = sun_entry->GetMemberValue<Color>(HashStr32("Color"), Color::FromRGBA(100, 100, 100, 1));
    }


    // Load objects

    ConfigEntry* object_list = info.GetEntry(HashStr32("Objects"));

    for (const ConfigEntry& object_entry : object_list->Members) {
        if (first_time) {
            AddObjectFromEntry(path, object_entry, scene);
        }
        else {
            TSRef<Object> object = scene.FindObject(object_entry.Name.GetHash());
            ApplyPropertiesToObject(object, object_entry);
        }
    }
}


void SceneFile::AddObjectFromEntry(const std::string& scene_path, const ConfigEntry& object_entry, Scene& scene)
{
    const char* mesh_path = object_entry.GetMember(HashStr32("Mesh"))->Get<const char*>();


    LoadObjectOptions load_options {};
    if (object_entry.GetMember(HashStr32("Physics")) != nullptr) {
        load_options.bGeneratePhysicsMesh = true;
    }

    TSRef<Object> object = gAssetManager->LoadObject(object_entry.Name.Get(), scene_path + mesh_path, load_options);
    object->pScene = &scene;

    ApplyPropertiesToObject(object, object_entry);

    scene.Attach(object);
}


void SceneFile::ApplyPropertiesToObject(TSRef<Object>& object, const ConfigEntry& object_entry)
{
    ConfigEntry* shadow_caster = object_entry.GetMember(HashStr32("Shadows"));
    if (shadow_caster != nullptr) {
        object->SetShadowCaster(static_cast<bool>(shadow_caster->Get<int64>()));
    }

    ConfigEntry* scale = object_entry.GetMember(HashStr32("Scale"));
    if (scale != nullptr) {
        object->SetScale(scale->Get<float32>());
    }

    object->SetPosition(object_entry.GetMemberValue(HashStr32("Pos"), object->mPosition));
    object->SetRotation(object_entry.GetMemberValue(HashStr32("Rot"), object->mRotation));
    object->SetScale(object_entry.GetMemberValue(HashStr32("Scale"), object->mScale));

    object->Update();
    object->MarkTransformOutOfDate();

    ConfigEntry* layer = object_entry.GetMember(HashStr32("Layer"));
    if (layer != nullptr) {
        int64 layer_value = layer->Get<int64>();

        if (layer_value == static_cast<int64>(eObjectLayer::PlayerLayer)) {
            object->SetObjectLayer(eObjectLayer::PlayerLayer);
        }
    }

    ConfigEntry* unlit = object_entry.GetMember(HashStr32("Unlit"));
    if (unlit != nullptr) {
        object->SetRenderUnlit(static_cast<bool>(unlit->Get<int64>()));
    }

    PhProperties physics_properties {};


    ConfigEntry* physics = object_entry.GetMember(HashStr32("Physics"));

    if (physics != nullptr && object->PhysicsId == PhObjectIdNull) {
        ePhMotionType motion_type = ePhMotionType::Static;

        ConfigEntry* type = physics->GetMember(HashStr32("Type"));
        if (type && type->Get<uint32>() == static_cast<uint32>(ePhMotionType::Dynamic)) {
            motion_type = ePhMotionType::Dynamic;
        }

        ConfigEntry* from_mesh = physics->GetMember(HashStr32("FromMesh"));
        if (from_mesh != nullptr && from_mesh->Get<bool>() == true) {
            object->PhysicsCreateMesh(nullptr, motion_type, physics_properties);
        }

        ConfigEntry* box_collider = physics->GetMember(HashStr32("BoxCollider"));
        if (box_collider != nullptr) {
            Vec3f box_size = box_collider->GetMemberValue(HashStr32("Size"), Vec3f::sOne);
            object->PhysicsCreatePrimitive(ePhPrimitiveType::Box, box_size, motion_type, physics_properties);
        }
    }

    object->OnLoaded(
        [](TSRef<AxBase> base_asset)
        {
            TSRef<Object> obj = base_asset;
            obj->PrintDebug();
        });
}

} // namespace fx
