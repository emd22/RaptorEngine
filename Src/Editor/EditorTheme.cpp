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

static bool ReadColor(const ConfigEntry& entry, Color& out_color)
{
	const PagedArray<ConfigPrimitive>& values = entry.GetArrayData();

	if (!entry.bIsArray || (values.Size() != 3 && values.Size() != 4)) {
		return false;
	}

	const auto component = [&values](uint32 index)
	{ return static_cast<uint8>(std::clamp(values[index].Get<int32>(), 0, 255)); };

	uint8 alpha = 255;

	if (values.Size() == 4) {
		alpha = component(3);
	}

	out_color = Color::FromRGBA(component(0), component(1), component(2), alpha);

	return true;
}

bool EditorTheme::Load(const String& path)
{
	ConfigFile config;
	config.Load(path.Str());

	if (config.HasErrors() || config.GetEntries().Size() == 0) {
		LogWarning(LC_CORE, "Could not read editor theme '{}'", path.CStr());
		return false;
	}

	if (const ConfigEntry* name = config.GetEntry(HashStr32("name"))) {
		Name = String(name->GetValue<const char*>());
	}

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

	ForEachColor(
		[&](const char* group_name, const char* key, Color& color)
		{
			const ConfigEntry* group = config.GetEntry(HashStr32(group_name));

			if (group == nullptr) {
				return;
			}

			const ConfigEntry* entry = group->GetMember(HashStr32(key));

			if (entry == nullptr) {
				return;
			}

			if (!ReadColor(*entry, color)) {
				LogWarning(LC_CORE, "Editor theme '{}': {}.{} should be [r, g, b] or [r, g, b, a]", path.CStr(),
						   group_name, key);
			}
		});

	ForEachInt(
		[&](const char* group_name, const char* key, int32& value)
		{
			const ConfigEntry* group = config.GetEntry(HashStr32(group_name));

			if (group == nullptr) {
				return;
			}

			const ConfigEntry* entry = group->GetMember(HashStr32(key));

			if (entry == nullptr) {
				return;
			}

			if (entry->bIsArray || (entry->Type != ConfigPrimitive::ePrimitiveType::Int &&
									entry->Type != ConfigPrimitive::ePrimitiveType::Float)) {
				LogWarning(LC_CORE, "Editor theme '{}': {}.{} should be a number", path.CStr(), group_name, key);
				return;
			}

			value = entry->GetValue<int32>();
		});

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
	mThemes.clear();
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
			mThemes.push_back(std::move(theme));
		}
	}

	if (mThemes.empty()) {
		LogWarning(LC_CORE, "No editor themes found in {}, using the built-in colours", scThemeDirectory);

		EditorTheme theme;
		theme.ID = String(scDefaultThemeID);
		theme.Name = theme.ID;
		mThemes.push_back(std::move(theme));
	}

	const String stored_id = ReadStoredThemeID();

	for (uint32 index = 0; index < mThemes.size(); index++) {
		if (mThemes[index].ID == stored_id) {
			mActiveIndex = index;
		}
	}
}

const EditorTheme& EditorThemeLibrary::GetActive() const
{
	static const EditorTheme sDefault {};

	if (mActiveIndex >= mThemes.size()) {
		return sDefault;
	}

	return mThemes[mActiveIndex];
}

bool EditorThemeLibrary::ReloadActive()
{
	if (mActiveIndex >= mThemes.size()) {
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
	if (index < mThemes.size()) {
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
