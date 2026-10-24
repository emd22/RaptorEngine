#include "EditorTheme.hpp"

#ifdef FX_IS_EDITOR

#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Hash.hpp>
#include <Core/Log.hpp>
#include <algorithm>
#include <filesystem>

namespace fx::editor {

static constexpr const char* scMainConfigPath = "Config/Main.conf";
static constexpr const char* scThemeConfigKey = "editor_theme";
static constexpr const char* scDefaultThemeID = "Dark";

bool EditorTheme::Load(const String& path)
{
	ConfigFile config;
	config.Load(path.Str());

	if (config.HasErrors() || config.GetEntries().Size() == 0) {
		LogWarning(LC_CORE, "Could not read editor theme '{}'", path.CStr());
		return false;
	}

	Serializable<EditorTheme>::LoadFromConfig(config, *this);

	if (const ConfigEntry* appearance = config.GetEntry(HashStr32("appearance"))) {
		String value;

		if (appearance->Type == ConfigPrimitive::ePrimitiveType::String) {
			value = String(appearance->GetValue<const char*>());
		}

		if (value == "light") {
			Appearance = eAppearance::Light;
		}
		else if (value == "dark") {
			Appearance = eAppearance::Dark;
		}
		else if (value == "system") {
			Appearance = eAppearance::System;
		}
		else {
			LogWarning(LC_CORE, "Editor theme '{}': appearance should be \"light\", \"dark\" or \"system\"",
					   path.CStr());
		}
	}

	return true;
}

static String ReadStoredThemeID()
{
	ConfigFile config;
	config.Load(scMainConfigPath);

	const ConfigEntry* entry = config.GetEntry(HashStr32(scThemeConfigKey));

	if (entry == nullptr || entry->Type != ConfigPrimitive::ePrimitiveType::String) {
		return String(scDefaultThemeID);
	}

	return String(entry->GetValue<const char*>());
}

void EditorThemeLibrary::Load()
{
	mThemes.Clear();
	mActiveIndex = 0;

	const std::filesystem::path directory(FilesystemIO::ResolvePath(scThemeDirectory).CStr());

	std::vector<std::filesystem::path> files;
	std::error_code error;

	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error)) {
		if (entry.is_regular_file() && entry.path().extension() == ".conf") {
			files.push_back(entry.path());
		}
	}

	std::sort(files.begin(), files.end());

	for (const std::filesystem::path& file : files) {
		EditorTheme theme;
		theme.ID = String(file.stem().string());
		theme.Name = theme.ID;

		const std::string relative_path = std::string(scThemeDirectory) + "/" + file.filename().string();

		if (theme.Load(String(relative_path))) {
			mThemes.Insert(std::move(theme));
		}
	}

	if (mThemes.IsEmpty()) {
		LogWarning(LC_CORE, "No editor themes found in {}, using the built-in colours", scThemeDirectory);

		EditorTheme theme;
		theme.ID = String(scDefaultThemeID);
		theme.Name = theme.ID;
		mThemes.Insert(std::move(theme));
	}

	const String stored_id = ReadStoredThemeID();

	for (uint32 index = 0; index < mThemes.Size; index++) {
		if (mThemes[index].ID == stored_id) {
			mActiveIndex = index;
		}
	}
}

const EditorTheme& EditorThemeLibrary::GetActive() const
{
	static const EditorTheme sDefault {};

	if (mActiveIndex >= mThemes.Size) {
		return sDefault;
	}

	return mThemes[mActiveIndex];
}

bool EditorThemeLibrary::ReloadActive()
{
	if (mActiveIndex >= mThemes.Size) {
		return false;
	}

	EditorTheme& active = mThemes[mActiveIndex];

	EditorTheme theme;
	theme.ID = active.ID;
	theme.Name = theme.ID;

	const String path(std::string(scThemeDirectory) + "/" + active.ID.CStr() + ".conf");

	if (!theme.Load(path)) {
		return false;
	}

	active = std::move(theme);

	return true;
}

void EditorThemeLibrary::Select(uint32 index)
{
	if (index < mThemes.Size) {
		mActiveIndex = index;
	}
}

EditorThemeLibrary& GetThemeLibrary()
{
	static EditorThemeLibrary sLibrary;
	return sLibrary;
}

const EditorTheme& GetEditorTheme() { return GetThemeLibrary().GetActive(); }

void StoreThemeChoice(const String& id)
{
	ConfigFile config {};
	config.Load(scMainConfigPath);

	if (config.HasErrors() || config.GetEntries().Size() == 0) {
		LogWarning(LC_CORE, "Could not store the editor theme in {}", scMainConfigPath);
		return;
	}

	ConfigEntry* entry = config.GetEntry(HashStr32(scThemeConfigKey));

	if (entry != nullptr) {
		entry->Set(id);
	}
	else {
		config.AddEntry(ConfigEntry::Literal(scThemeConfigKey, id.CStr()));
	}

	config.Write(scMainConfigPath);
}

} // namespace fx::editor

#endif
