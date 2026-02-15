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

    FxConfigEntry* meta = info.GetEntry(FxHashStr64("meta"));

    if (!meta) {
        FxLogError("Project missing metadata!");
        return;
    }

    scene.Name = meta->GetMember(FxHashStr64("name"))->Get<const char*>();

    // Load objects

    FxConfigEntry* object_list = info.GetEntry(FxHashStr64("objects"));

    for (const FxConfigEntry& object_entry : object_list->Members) {
        AddObjectFromEntry(path, object_entry, scene);
    }
}


void FxSceneFile::AddObjectFromEntry(const std::string& scene_path, const FxConfigEntry& object_entry, FxScene& scene)
{
    const char* mesh_path = object_entry.GetMember(FxHashStr64("mesh"))->Get<const char*>();

    FxRef<FxObject> object = AxManager::LoadObject(object_entry.Name.Get(), scene_path + mesh_path);

    FxConfigEntry* shadow_caster = object_entry.GetMember(FxHashStr64("shadows"));
    if (shadow_caster != nullptr) {
        object->SetShadowCaster(static_cast<bool>(shadow_caster->Get<int64>()));
    }

    FxConfigEntry* scale = object_entry.GetMember(FxHashStr64("scale"));
    if (scale != nullptr) {
        object->Scale(scale->Get<float32>());
    }

    FxConfigEntry* position = object_entry.GetMember(FxHashStr64("pos"));
    if (position != nullptr) {
        object->MoveTo(position->GetVec3f());
    }

    FxConfigEntry* rotation = object_entry.GetMember(FxHashStr64("rot"));
    if (rotation != nullptr) {
        object->mRotation = rotation->GetQuat();
        object->MarkTransformOutOfDate();
    }

    scene.Attach(object);
}
