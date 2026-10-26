#include "Presets.hpp"

#include <Asset/ConfigFile.hpp>
#include <Core/FilesystemIO.hpp>

namespace fx::mapc {

namespace {

constexpr int64 scPresetFileVersion = 1;

constexpr const char* scDenoiseNames[] = { "off", "low", "medium", "high" };

int64 ToInt(bool value)
{
	if (value) {
		return 1;
	}

	return 0;
}

ConfigEntry WritePreset(const CompilePreset& preset, uint32 index)
{
	const CompileOptions& options = preset.Options;
	const LightmapOptions& lightmap = options.Lightmap;

	ConfigEntry entry = ConfigEntry::Struct(String::Fmt("{}", index));

	entry.AddMember(ConfigEntry::Literal("name", preset.Name.CStr()));

	entry.AddMember(ConfigEntry::Literal("cull_hidden", ToInt(options.Surface.bCullHidden)));
	entry.AddMember(ConfigEntry::Literal("merge", ToInt(options.Surface.bMerge)));
	entry.AddMember(ConfigEntry::Literal("fix_tjunctions", ToInt(options.Surface.bFixTJunctions)));
	entry.AddMember(ConfigEntry::Literal("chunk_size", static_cast<float32>(options.Surface.ChunkSize)));
	entry.AddMember(ConfigEntry::Literal("keep_outside", ToInt(options.bKeepOutside)));

	entry.AddMember(ConfigEntry::Literal("place_probes", ToInt(options.bPlaceProbes)));
	entry.AddMember(ConfigEntry::Literal("probe_level_spacing", options.ProbeSettings.LevelSpacing));
	entry.AddMember(ConfigEntry::Literal("probe_brush_spacing", options.ProbeSettings.BrushSpacing));

	entry.AddMember(ConfigEntry::Literal("lightmap", ToInt(lightmap.bEnabled)));
	entry.AddMember(ConfigEntry::Literal("lightmap_density", static_cast<float32>(lightmap.Density)));
	entry.AddMember(ConfigEntry::Literal("lightmap_size", static_cast<int64>(lightmap.MaxSize)));
	entry.AddMember(ConfigEntry::Literal("lightmap_samples", static_cast<int64>(lightmap.Samples)));
	entry.AddMember(ConfigEntry::Literal("sun_radius", static_cast<float32>(lightmap.SunRadiusDegrees)));
	entry.AddMember(ConfigEntry::Literal("probe_texel_rays", static_cast<int64>(lightmap.ProbeTexelRays)));
	entry.AddMember(ConfigEntry::Literal("fog_shadow", ToInt(lightmap.FogShadow.bEnabled)));
	entry.AddMember(ConfigEntry::Literal("fog_shadow_texel", static_cast<float32>(lightmap.FogShadow.TexelSize)));

	entry.AddMember(ConfigEntry::Literal("indirect", ToInt(lightmap.bIndirect)));
	entry.AddMember(ConfigEntry::Literal("indirect_rays", static_cast<int64>(lightmap.IndirectRays)));
	entry.AddMember(ConfigEntry::Literal("indirect_bounces", static_cast<int64>(lightmap.IndirectBounces)));
	entry.AddMember(ConfigEntry::Literal("indirect_cache", static_cast<int64>(lightmap.IndirectCacheMegabytes)));
	entry.AddMember(ConfigEntry::Literal("indirect_denoise", GetDenoiseQualityName(lightmap.IndirectDenoise)));

	entry.AddMember(ConfigEntry::Literal("verify_rays", static_cast<int64>(options.VerifyRays)));

	return entry;
}

bool ReadBool(const ConfigEntry& entry, const char* name, bool fallback)
{
	return entry.GetMemberValue<int64>(HashStr32(name), ToInt(fallback)) != 0;
}

uint32 ReadUInt(const ConfigEntry& entry, const char* name, uint32 fallback)
{
	const int64 value = entry.GetMemberValue<int64>(HashStr32(name), fallback);

	if (value < 0) {
		return 0;
	}

	return static_cast<uint32>(value);
}

double ReadDouble(const ConfigEntry& entry, const char* name, double fallback)
{
	return entry.GetMemberValue<float32>(HashStr32(name), static_cast<float32>(fallback));
}

CompilePreset ReadPreset(const ConfigEntry& entry)
{
	CompilePreset preset;
	preset.Name = entry.GetMemberValue<const char*>(HashStr32("name"), "");

	CompileOptions& options = preset.Options;
	LightmapOptions& lightmap = options.Lightmap;

	options.Surface.bCullHidden = ReadBool(entry, "cull_hidden", options.Surface.bCullHidden);
	options.Surface.bMerge = ReadBool(entry, "merge", options.Surface.bMerge);
	options.Surface.bFixTJunctions = ReadBool(entry, "fix_tjunctions", options.Surface.bFixTJunctions);
	options.Surface.ChunkSize = ReadDouble(entry, "chunk_size", options.Surface.ChunkSize);
	options.bKeepOutside = ReadBool(entry, "keep_outside", options.bKeepOutside);

	options.bPlaceProbes = ReadBool(entry, "place_probes", options.bPlaceProbes);
	options.ProbeSettings.LevelSpacing = static_cast<float32>(
		ReadDouble(entry, "probe_level_spacing", options.ProbeSettings.LevelSpacing));
	options.ProbeSettings.BrushSpacing = static_cast<float32>(
		ReadDouble(entry, "probe_brush_spacing", options.ProbeSettings.BrushSpacing));

	lightmap.bEnabled = ReadBool(entry, "lightmap", lightmap.bEnabled);
	lightmap.Density = ReadDouble(entry, "lightmap_density", lightmap.Density);
	lightmap.MaxSize = ReadUInt(entry, "lightmap_size", lightmap.MaxSize);
	lightmap.Samples = ReadUInt(entry, "lightmap_samples", lightmap.Samples);
	lightmap.SunRadiusDegrees = ReadDouble(entry, "sun_radius", lightmap.SunRadiusDegrees);
	lightmap.ProbeTexelRays = ReadUInt(entry, "probe_texel_rays", lightmap.ProbeTexelRays);
	lightmap.FogShadow.bEnabled = ReadBool(entry, "fog_shadow", lightmap.FogShadow.bEnabled);
	lightmap.FogShadow.TexelSize = ReadDouble(entry, "fog_shadow_texel", lightmap.FogShadow.TexelSize);

	lightmap.bIndirect = ReadBool(entry, "indirect", lightmap.bIndirect);
	lightmap.IndirectRays = ReadUInt(entry, "indirect_rays", lightmap.IndirectRays);
	lightmap.IndirectBounces = ReadUInt(entry, "indirect_bounces", lightmap.IndirectBounces);
	lightmap.IndirectCacheMegabytes = ReadUInt(entry, "indirect_cache", lightmap.IndirectCacheMegabytes);

	const String denoise = entry.GetMemberValue<const char*>(HashStr32("indirect_denoise"),
															 GetDenoiseQualityName(lightmap.IndirectDenoise));

	if (!ParseDenoiseQuality(denoise, lightmap.IndirectDenoise)) {
		LogWarning(LC_CORE, "Preset '{}' has an unknown denoise level '{}'", preset.Name, denoise);
	}

	options.VerifyRays = ReadUInt(entry, "verify_rays", options.VerifyRays);

	return preset;
}

} // namespace

bool PresetLibrary::Load(const String& path)
{
	mPresets.Clear();

	if (!FilesystemIO::FileExists(FilesystemIO::ResolvePath(path))) {
		return true;
	}

	ConfigFile config;
	config.Load(path.CStr());

	if (config.HasErrors()) {
		return false;
	}

	const ConfigEntry* presets = config.GetEntry(HashStr32("presets"));

	if (presets == nullptr || !presets->IsObjectArray()) {
		return true;
	}

	for (const ConfigEntry& entry : presets->GetAllMembers()) {
		CompilePreset preset = ReadPreset(entry);

		if (preset.Name.IsEmpty() || Find(preset.Name) != nullptr) {
			continue;
		}

		mPresets.Emplace(std::move(preset));
	}

	return true;
}

bool PresetLibrary::Save(const String& path) const
{
	ConfigFile config;
	config.AddEntry(ConfigEntry::Literal("version", scPresetFileVersion));

	if (mPresets.Size > 0) {
		ConfigEntry presets = ConfigEntry::Array("presets", ConfigPrimitive::ePrimitiveType::Struct);

		for (uint32 i = 0; i < mPresets.Size; i++) {
			presets.AddMember(WritePreset(mPresets[i], i));
		}

		config.AddEntry(std::move(presets));
	}

	return config.Write(FilesystemIO::ResolvePath(path).CStr());
}

const CompilePreset* PresetLibrary::Find(const String& name) const
{
	for (const CompilePreset& preset : mPresets) {
		if (preset.Name == name) {
			return &preset;
		}
	}

	return nullptr;
}

void PresetLibrary::Store(const String& name, const CompileOptions& options)
{
	CompilePreset preset { name, options };
	preset.Options.InputPath = "";
	preset.Options.OutputPath = "";
	preset.Options.ObjPath = "";
	preset.Options.Lightmap.ImagePath = "";

	for (CompilePreset& existing : mPresets) {
		if (existing.Name == name) {
			existing = std::move(preset);
			return;
		}
	}

	mPresets.Emplace(std::move(preset));
}

bool PresetLibrary::Remove(const String& name)
{
	for (uint32 i = 0; i < mPresets.Size; i++) {
		if (mPresets[i].Name == name) {
			RemoveAt(mPresets, i);
			return true;
		}
	}

	return false;
}

bool ParseDenoiseQuality(const String& name, eDenoiseQuality& out_quality)
{
	for (uint32 i = 0; i < std::size(scDenoiseNames); i++) {
		if (name == scDenoiseNames[i]) {
			out_quality = static_cast<eDenoiseQuality>(i);
			return true;
		}
	}

	return false;
}

const char* GetDenoiseQualityName(eDenoiseQuality quality)
{
	const uint32 index = static_cast<uint32>(quality);

	if (index >= std::size(scDenoiseNames)) {
		return scDenoiseNames[static_cast<uint32>(eDenoiseQuality::Medium)];
	}

	return scDenoiseNames[index];
}

} // namespace fx::mapc
