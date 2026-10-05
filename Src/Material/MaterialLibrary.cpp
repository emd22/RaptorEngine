#include "MaterialLibrary.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <filesystem>
#include <vector>

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

MaterialLibrary::MaterialLibrary() : mpLibrary(rx_material_library_new()) {}

bool MaterialLibrary::Load(const std::string& list_path, const std::string& texture_root)
{
	std::vector<uint8> bytes;

	if (!ReadConfigFileBytes(list_path, bytes)) {
		LogError(LC_ASSET, "The material list '{}' has no `list` entry", list_path);
		return false;
	}

	const RxHost host = MakeConfigHost();
	const std::string constants_path = GetConfigConstantsPath();

	if (!rx_material_library_parse(mpLibrary.get(), bytes.data(), bytes.size(), constants_path.c_str(), &host)) {
		LogError(LC_ASSET, "The material list '{}' has no `list` entry", list_path);
		return false;
	}

	const uint32 count = rx_material_library_def_count(mpLibrary.get());

	for (uint32 i = 0; i < count; i++) {
		const RxMaterialDef& def = *rx_material_library_def(mpLibrary.get(), i);

		const MaterialID id = gMaterialManager->NewMaterial(def.name, false);
		Material* material = gMaterialManager->GetMaterial(id);

		AttachTexture(material, Material::eResourceType::Diffuse, texture_root, def.diffuse, def.name);
		AttachTexture(material, Material::eResourceType::Normal, texture_root, def.normal, def.name);
		const bool has_orm = AttachTexture(material, Material::eResourceType::ORM, texture_root, def.orm, def.name);

		if (has_orm) {
			material->SetMetallicRoughness(1.0f, 1.0f);
		}

		material->Finalize();

		rx_material_library_register(mpLibrary.get(), def.name, id.GetID());
	}

	LogInfo(LC_ASSET, "Loaded {} materials from '{}'", count, list_path);

	return true;
}

} // namespace fx
