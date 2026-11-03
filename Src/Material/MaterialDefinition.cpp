#include "MaterialDefinition.hpp"

#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Hash.hpp>
#include <Core/Log.hpp>
#include <Material/SpecialMaterial.hpp>
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fx {

static String ReadTexturePath(const ConfigEntry* entry, const char* key, const String& directory,
							  const std::filesystem::path& resolved_directory, const String& stem, const char* suffix)
{
	const ConfigEntry* member = nullptr;

	if (entry != nullptr) {
		member = entry->GetMember(HashStr32(key));
	}

	if (member != nullptr) {
		if (member->Type != ConfigEntry::ePrimitiveType::String) {
			return String();
		}

		const char* relative_path = member->GetValue<const char*>();

		if (relative_path == nullptr || relative_path[0] == '\0') {
			return String();
		}

		return directory + "/" + relative_path;
	}

	const String conventional_name = stem + "_" + suffix + ".ktx2";
	std::error_code error;

	if (!std::filesystem::exists(resolved_directory / conventional_name.CStr(), error)) {
		return String();
	}

	return directory + "/" + conventional_name;
}

static String GetDefinitionName(const String& stem)
{
	const eSpecialMaterial special = FindSystemMaterial(stem.CStr());

	if (special != eSpecialMaterial::None) {
		return GetSpecialMaterialName(special);
	}

	return stem;
}

static MaterialDefinition ReadDefinition(const std::filesystem::path& file_path, const String& directory,
										 const String& source_path)
{
	const String stem(file_path.stem().string());
	const std::filesystem::path resolved_directory = file_path.parent_path();

	ConfigFile file {};
	file.Load(source_path.CStr());

	const ConfigEntry* entry = file.GetEntry(HashStr32("material"));

	MaterialDefinition definition {};
	definition.Name = GetDefinitionName(stem);
	definition.SourcePath = source_path;

	definition.DiffusePath = ReadTexturePath(entry, "diffuse", directory, resolved_directory, stem, "diffuse");
	definition.NormalPath = ReadTexturePath(entry, "normal", directory, resolved_directory, stem, "normal");
	definition.OrmPath = ReadTexturePath(entry, "orm", directory, resolved_directory, stem, "orm");

	if (entry == nullptr) {
		return definition;
	}

	definition.Alpha = entry->GetMemberValue<float32>(HashStr32("alpha"), 1.0f);
	definition.bUnlit = entry->GetMemberValue(HashStr32("unlit"), 0) != 0;
	definition.bGlass = entry->GetMemberValue(HashStr32("glass"), 0) != 0;

	if (entry->GetMember(HashStr32("surface")) != nullptr) {
		definition.Surface = String(entry->GetMemberValue<const char*>(HashStr32("surface"), ""));
	}

	if (entry->GetMember(HashStr32("density")) != nullptr) {
		const float32 density = entry->GetMemberValue<float32>(HashStr32("density"), 0.0f);

		if (density > 0.0f) {
			definition.Density = density;
		}
		else {
			LogWarning(LC_ASSET, "The material '{}' has a density of {}, it must be above zero", definition.Name,
					   density);
		}
	}

	if (entry->GetMember(HashStr32("metallic")) != nullptr) {
		definition.Metallic = entry->GetMemberValue<float32>(HashStr32("metallic"), 0.0f);
	}

	if (entry->GetMember(HashStr32("roughness")) != nullptr) {
		definition.Roughness = entry->GetMemberValue<float32>(HashStr32("roughness"), 1.0f);
	}

	if (entry->GetMember(HashStr32("color")) != nullptr) {
		definition.BaseColor = entry->GetMemberValue<Vec3f>(HashStr32("color"), Vec3f(1.0f, 1.0f, 1.0f));
	}

	return definition;
}

MaterialDefinitionArray LoadMaterialDefinitions(const String& root)
{
	MaterialDefinitionArray definitions;

	const std::filesystem::path resolved_root = FilesystemIO::ResolvePath(root).Str();
	std::error_code error;

	if (!std::filesystem::is_directory(resolved_root, error)) {
		LogError(LC_ASSET, "The material directory '{}' does not exist", root);
		return definitions;
	}

	DynArray<std::filesystem::path, GrowthFunctions::Double> files;

	for (std::filesystem::recursive_directory_iterator it(resolved_root, error), end; !error && it != end;
		 it.increment(error)) {
		if (it->is_regular_file(error) && it->path().extension() == MaterialDefinition::scExtension) {
			files.Insert(it->path());
		}
	}

	std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b)
			  { return a.stem().string() < b.stem().string(); });

	for (const std::filesystem::path& file_path : files) {
		const String relative_directory(file_path.parent_path().lexically_relative(resolved_root).generic_string());

		String directory = root;

		if (!relative_directory.IsEmpty() && relative_directory != ".") {
			directory += String("/") + relative_directory;
		}

		const String source_path = directory + "/" + file_path.filename().string().c_str();
		const String name = GetDefinitionName(String(file_path.stem().string()));

		const bool is_duplicate = std::any_of(definitions.begin(), definitions.end(),
											  [&name](const MaterialDefinition& other)
											  { return other.Name == name.CStr(); });

		if (is_duplicate) {
			LogWarning(LC_ASSET, "Skipping the material '{}': the name '{}' is already defined", source_path, name);
			continue;
		}

		definitions.Insert(ReadDefinition(file_path, directory, source_path));
	}

	return definitions;
}

} // namespace fx
