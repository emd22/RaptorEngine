#include "MaterialLibrary.hpp"

#include <cstring>

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Hash.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <filesystem>

namespace fx {

static bool AttachTexture(Material* material, Material::eResourceType type, const std::string& texture_root,
						  const char* relative_path, const char* material_name)
{
	if (relative_path[0] == '\0') {
		return false;
	}

	const std::string path = texture_root + relative_path;

	if (!std::filesystem::exists(FilesystemIO::ResolvePath(path))) {
		LogWarning(LC_ASSET, "Material '{}' is missing the texture '{}'", material_name, path);
		return false;
	}

	AssetTicket ticket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, path,
												  eImageCreateFlags::None);
	material->Attach(type, ticket);

	return true;
}

bool MaterialLibrary::Load(const std::string& list_path, const std::string& texture_root)
{
	mEntries.Clear();
	mEntries.Create(64);

	ConfigFile file {};
	file.Load(list_path);

	ConfigEntry* list = file.GetEntry(HashStr32("list"));

	if (list == nullptr) {
		LogError(LC_ASSET, "The material list '{}' has no `list` entry", list_path);
		return false;
	}

	for (const ConfigEntry& item : list->GetAllMembers()) {
		const char* name = item.GetMemberValue<const char*>(HashStr32("name"), "unnamed");

		const MaterialID id = gMaterialManager->NewMaterial(name, false);
		Material* material = gMaterialManager->GetMaterial(id);

		AttachTexture(material, Material::eResourceType::Diffuse, texture_root,
					  item.GetMemberValue<const char*>(HashStr32("diffuse"), ""), name);
		AttachTexture(material, Material::eResourceType::Normal, texture_root,
					  item.GetMemberValue<const char*>(HashStr32("normal"), ""), name);
		const bool has_orm = AttachTexture(material, Material::eResourceType::ORM, texture_root,
										   item.GetMemberValue<const char*>(HashStr32("orm"), ""), name);

		if (has_orm) {
			material->SetMetallicRoughness(1.0f, 1.0f);
		}

		material->Finalize();

		const char* diffuse_path = item.GetMemberValue<const char*>(HashStr32("diffuse"), "");
		const std::string diffuse = diffuse_path[0] != '\0' ? texture_root + diffuse_path : std::string();

		mEntries.Insert(Entry { .Name = name, .Material = id, .DiffusePath = diffuse });
	}

	LogInfo(LC_ASSET, "Loaded {} materials from '{}'", mEntries.Size(), list_path);

	return true;
}

const String& MaterialLibrary::GetName(uint32 id) const
{
	static const String scEmpty = "unknown";

	if (id >= mEntries.Size()) {
		return scEmpty;
	}

	return mEntries[id].Name;
}

const std::string& MaterialLibrary::GetDiffusePath(uint32 id) const
{
	static const std::string scEmpty;

	if (id >= mEntries.Size()) {
		return scEmpty;
	}

	return mEntries[id].DiffusePath;
}

MaterialID MaterialLibrary::GetMaterial(int32 id) const
{
	if (id < 0 || static_cast<size_t>(id) >= mEntries.Size()) {
		return MaterialID::scNull;
	}

	return mEntries[static_cast<size_t>(id)].Material;
}

int32 MaterialLibrary::FindID(const MaterialID& material) const
{
	if (material.IsNull()) {
		return -1;
	}

	for (size_t i = 0; i < mEntries.Size(); i++) {
		if (mEntries[i].Material == material) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

int32 MaterialLibrary::FindIDByName(const char* name) const
{
	if (name == nullptr) {
		return -1;
	}

	for (size_t i = 0; i < mEntries.Size(); i++) {
		if (strcmp(mEntries[i].Name.CStr(), name) == 0) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

} // namespace fx
