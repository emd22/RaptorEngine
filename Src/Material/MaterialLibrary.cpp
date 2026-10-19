#include "MaterialLibrary.hpp"

#include <Asset/AssetManager.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Hash.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>

namespace fx {


static bool AttachTexture(Material* material, Material::eResourceType type, const String& path,
						  const char* material_name)
{
	if (path.IsEmpty()) {
		return false;
	}

	if (!FilesystemIO::FileExists(FilesystemIO::ResolvePath(path))) {
		LogWarning(LC_ASSET, "Material '{}' is missing the texture '{}'", material_name, path);
		return false;
	}

	AssetTicket ticket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, path.Str(),
												  eImageCreateFlags::None);
	material->Attach(type, ticket);

	return true;
}

bool MaterialLibrary::Load(const std::string& root)
{
	mEntries.Clear();
	mEntries.Create(64);

	std::vector<MaterialDefinition> definitions = LoadMaterialDefinitions(root);

	for (MaterialDefinition& definition : definitions) {
		const Name entry_name(definition.Name.CStr());

		mEntries.Insert(Entry { .EntryName = entry_name, .Definition = std::move(definition) });
	}

	LogInfo(LC_ASSET, "Found {} materials in '{}'", mEntries.Size(), root);

	return mEntries.Size() > 0;
}

MaterialID MaterialLibrary::CreateMaterial(Entry& entry) const
{
	const MaterialDefinition& definition = entry.Definition;
	const char* name = definition.Name.CStr();

	const MaterialID id = gMaterialManager->NewMaterial(definition.Name, false);
	Material* material = gMaterialManager->GetMaterial(id);

	AttachTexture(material, Material::eResourceType::Diffuse, definition.DiffusePath, name);
	AttachTexture(material, Material::eResourceType::Normal, definition.NormalPath, name);
	const bool has_orm = AttachTexture(material, Material::eResourceType::ORM, definition.OrmPath, name);

	if (has_orm) {
		material->SetMetallicRoughness(1.0f, 1.0f);
	}

	if (definition.Alpha < 1.0f) {
		material->SetAlpha(definition.Alpha);
	}

	if (definition.bUnlit) {
		material->SetUnlit(true);
	}

	if (definition.bGlass) {
		material->SetGlass(true);
	}

	if (!has_orm) {
		const float32 metallic = definition.Metallic.value_or(material->Properties.MetallicFactor);
		const float32 roughness = definition.Roughness.value_or(material->Properties.RoughnessFactor);

		material->SetMetallicRoughness(metallic, roughness);
	}

	if (definition.BaseColor.has_value()) {
		const Vec3f color = *definition.BaseColor;
		const float32 base_color[3] = { color.X, color.Y, color.Z };

		material->SetBaseColorFactor(base_color);
	}

	material->Finalize();

	entry.Material = id;

	return id;
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

	return mEntries[static_cast<size_t>(id.ID)].Definition.DiffusePath;
}

MaterialID MaterialLibrary::GetMaterial(const MaterialLibraryID id) const
{
	if (!id.IsValid() || static_cast<size_t>(id.ID) >= mEntries.Size()) {
		return MaterialID::scNull;
	}

	Entry& entry = mEntries[static_cast<size_t>(id.ID)];

	if (entry.Material.IsNull()) {
		return CreateMaterial(entry);
	}

	return entry.Material;
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
