#include "MaterialLibrary.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Hash.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <cstring>
#include <filesystem>

namespace fx {


static bool AttachTexture(Material* material, Material::eResourceType type, const std::string& texture_root,
						  const char* relative_path, const char* material_name)
{
	if (relative_path[0] == '\0') {
		return false;
	}

	const std::string path = texture_root + relative_path;

	if (!FilesystemIO::FileExists(FilesystemIO::ResolvePath(path))) {
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

		mEntries.Insert(Entry { .EntryName = Name(name), .Material = id, .DiffusePath = diffuse });
	}

	LogInfo(LC_ASSET, "Loaded {} materials from '{}'", mEntries.Size(), list_path);

	return true;
}

const String& MaterialLibrary::GetName(const MaterialLibraryID id) const
{
	static const String scEmpty = "unknown";

	if (static_cast<size_t>(id.ID) >= mEntries.Size()) {
		return scEmpty;
	}

	return mEntries[static_cast<size_t>(id.ID)].EntryName.Get();
}

const String& MaterialLibrary::GetDiffusePath(const MaterialLibraryID id) const
{
	static const String scEmpty {};

	if (!id.IsValid() || static_cast<size_t>(id.ID) >= mEntries.Size()) {
		return scEmpty;
	}

	return mEntries[static_cast<size_t>(id.ID)].DiffusePath;
}

MaterialID MaterialLibrary::GetMaterial(const MaterialLibraryID id) const
{
	if (!id.IsValid() || static_cast<size_t>(id.ID) >= mEntries.Size()) {
		return MaterialID::scNull;
	}

	return mEntries[static_cast<size_t>(id.ID)].Material;
}

MaterialLibraryID MaterialLibrary::FindID(const MaterialID material) const
{
	if (material.IsNull()) {
		return MaterialLibraryID::scNull;
	}

	for (size_t i = 0; i < mEntries.Size(); i++) {
		if (mEntries[i].Material == material) {
			return MaterialLibraryID(i);
		}
	}

	return MaterialLibraryID::scNull;
}

MaterialLibraryID MaterialLibrary::FindIDByName(const char* name) const
{
	if (name == nullptr) {
		return MaterialLibraryID::scNull;
	}

	Hash32 name_hash = HashStr32(name);

	for (size_t i = 0; i < mEntries.Size(); i++) {
		if (mEntries[i].EntryName == name_hash) {
			return MaterialLibraryID(i);
		}
	}

	return MaterialLibraryID::scNull;
}

} // namespace fx
