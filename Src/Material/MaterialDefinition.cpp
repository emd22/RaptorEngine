#include "MaterialDefinition.hpp"

#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Hash.hpp>
#include <Core/Log.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fx {

static String ReadTexturePath(const ConfigEntry* entry, const char* key, const std::string& directory,
							  const std::filesystem::path& resolved_directory, const std::string& stem,
							  const char* suffix)
{
	const ConfigEntry* member = (entry != nullptr) ? entry->GetMember(HashStr32(key)) : nullptr;

	if (member != nullptr) {
		if (member->Type != ConfigEntry::ePrimitiveType::String) {
			return String();
		}

		const char* relative_path = member->GetValue<const char*>();

		if (relative_path == nullptr || relative_path[0] == '\0') {
			return String();
		}

		return String(directory + "/" + relative_path);
	}

	const std::string conventional_name = stem + "_" + suffix + ".ktx2";
	std::error_code error;

	if (!std::filesystem::exists(resolved_directory / conventional_name, error)) {
		return String();
	}

	return String(directory + "/" + conventional_name);
}

static MaterialDefinition ReadDefinition(const std::filesystem::path& file_path, const std::string& directory,
										 const std::string& source_path)
{
	const std::string stem = file_path.stem().string();
	const std::filesystem::path resolved_directory = file_path.parent_path();

	ConfigFile file {};
	file.Load(source_path);

	const ConfigEntry* entry = file.GetEntry(HashStr32("material"));

	MaterialDefinition definition {};
	definition.Name = String(stem);
	definition.SourcePath = String(source_path);

	definition.DiffusePath = ReadTexturePath(entry, "diffuse", directory, resolved_directory, stem, "diffuse");
	definition.NormalPath = ReadTexturePath(entry, "normal", directory, resolved_directory, stem, "normal");
	definition.OrmPath = ReadTexturePath(entry, "orm", directory, resolved_directory, stem, "orm");

	if (entry == nullptr) {
		return definition;
	}

	definition.Alpha = entry->GetMemberValue<float32>(HashStr32("alpha"), 1.0f);
	definition.bUnlit = entry->GetMemberValue(HashStr32("unlit"), 0) != 0;
	definition.bGlass = entry->GetMemberValue(HashStr32("glass"), 0) != 0;

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

std::vector<MaterialDefinition> LoadMaterialDefinitions(const std::string& root)
{
	std::vector<MaterialDefinition> definitions;

	const std::filesystem::path resolved_root = FilesystemIO::ResolvePath(String(root)).Str();
	std::error_code error;

	if (!std::filesystem::is_directory(resolved_root, error)) {
		LogError(LC_ASSET, "The material directory '{}' does not exist", root);
		return definitions;
	}

	std::vector<std::filesystem::path> files;

	for (std::filesystem::recursive_directory_iterator it(resolved_root, error), end; !error && it != end;
		 it.increment(error)) {
		if (it->is_regular_file(error) && it->path().extension() == MaterialDefinition::scExtension) {
			files.push_back(it->path());
		}
	}

	std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b)
			  { return a.stem().string() < b.stem().string(); });

	for (const std::filesystem::path& file_path : files) {
		const std::string relative_directory = file_path.parent_path().lexically_relative(resolved_root).generic_string();

		std::string directory = root;

		if (!relative_directory.empty() && relative_directory != ".") {
			directory += "/" + relative_directory;
		}

		const std::string source_path = directory + "/" + file_path.filename().string();
		const std::string stem = file_path.stem().string();

		const bool is_duplicate = std::any_of(definitions.begin(), definitions.end(),
											  [&stem](const MaterialDefinition& other)
											  { return other.Name == stem.c_str(); });

		if (is_duplicate) {
			LogWarning(LC_ASSET, "Skipping the material '{}': the name '{}' is already defined", source_path, stem);
			continue;
		}

		definitions.push_back(ReadDefinition(file_path, directory, source_path));
	}

	return definitions;
}

} // namespace fx
