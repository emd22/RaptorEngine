#include "FxSceneFile.hpp"

#include <Asset/AxManager.hpp>

// void FxSceneFile::Save(const FxScene& scene) {}

// void FxSceneFile::SaveObject(FxConfigEntry& parent, const FxObject& object)
// {
//     FxConfigEntry entry = FxConfigEntry::Struct(object.Name);

//     FxConfigEntry positions = FxConfigEntry::Array("pos", FxConfigEntry::ValueType::eFloat);
//     positions.AppendValue(object.mPosition);
//     entry.AddMember(std::move(positions));

//     FxConfigEntry rotation = FxConfigEntry::Array("rot", FxConfigEntry::ValueType::eFloat);
//     rotation.AppendValue(object.mRotation);
//     entry.AddMember(std::move(rotation));

//     parent.AddMember(std::move(entry));
// }


void FxSceneFile::Load(const std::string& path, FxScene& scene)
{
    FxConfigFile info;

    info.Load(path + "/info.prx");

    bool first_time = (scene.GetAllObjects().Size() == 0);

    if (first_time) {
        FxConfigEntry* meta = info.GetEntry(FxHashStr64("Meta"));

        if (!meta) {
            FxLogError("Project missing metadata!");
            return;
        }

        scene.Name = meta->GetMember(FxHashStr64("Name"))->Get<const char*>();
    }

    // Load objects

    FxConfigEntry* object_list = info.GetEntry(FxHashStr64("Objects"));

    for (const FxConfigEntry& object_entry : object_list->Members) {
        if (first_time) {
            AddObjectFromEntry(path, object_entry, scene);
        }
        else {
            FxRef<FxObject> object = scene.FindObject(object_entry.Name.GetHash());
            ApplyPropertiesToObject(object, object_entry);
        }
    }
}


void FxSceneFile::AddObjectFromEntry(const std::string& scene_path, const FxConfigEntry& object_entry, FxScene& scene)
{
    const char* mesh_path = object_entry.GetMember(FxHashStr64("Mesh"))->Get<const char*>();

    FxRef<FxObject> object = AxManager::LoadObject(object_entry.Name.Get(), scene_path + mesh_path);

    ApplyPropertiesToObject(object, object_entry);

    scene.Attach(object);
}


void FxSceneFile::ApplyPropertiesToObject(const FxRef<FxObject>& object, const FxConfigEntry& object_entry)
{
    FxConfigEntry* shadow_caster = object_entry.GetMember(FxHashStr64("Shadows"));
    if (shadow_caster != nullptr) {
        object->SetShadowCaster(static_cast<bool>(shadow_caster->Get<int64>()));
    }

    FxConfigEntry* scale = object_entry.GetMember(FxHashStr64("Scale"));
    if (scale != nullptr) {
        object->SetScale(scale->Get<float32>());
    }

    FxConfigEntry* position = object_entry.GetMember(FxHashStr64("Pos"));
    if (position != nullptr) {
        object->MoveTo(position->GetVec3f());
    }

    FxConfigEntry* rotation = object_entry.GetMember(FxHashStr64("Rot"));
    if (rotation != nullptr) {
        object->mRotation = rotation->GetQuat();
        object->MarkTransformOutOfDate();
    }

    FxConfigEntry* layer = object_entry.GetMember(FxHashStr64("Layer"));
    if (layer != nullptr) {
        int64 layer_value = layer->Get<int64>();

        if (layer_value == static_cast<int64>(FxObjectLayer::ePlayerLayer)) {
            object->SetObjectLayer(FxObjectLayer::ePlayerLayer);
        }
    }
}
