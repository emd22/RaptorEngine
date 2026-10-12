#include "RxModelConfig.hpp"

#include <Asset/ConfigFile.hpp>
#include <Core/Hash.hpp>
#include <Core/Log.hpp>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>

namespace fx {

namespace loader {

namespace {

using eType = ConfigPrimitive::ePrimitiveType;

bool IsTable(const ConfigEntry& entry) { return entry.Type == eType::Struct && !entry.bIsArray; }
bool IsString(const ConfigEntry& entry) { return entry.Type == eType::String && !entry.bIsArray; }
bool IsNumber(const ConfigPrimitive& value) { return value.Type == eType::Int || value.Type == eType::Float; }
bool IsNumber(const ConfigEntry& entry)
{
	return !entry.bIsArray && IsNumber(static_cast<const ConfigPrimitive&>(entry));
}
bool IsBool(const ConfigEntry& entry) { return entry.Type == eType::Int && !entry.bIsArray; }
bool IsValueArray(const ConfigEntry& entry) { return entry.bIsArray && !entry.IsObjectArray(); }

uint32 GetArraySize(const ConfigEntry& entry)
{
	return entry.ArrayData.IsInited() ? static_cast<uint32>(entry.ArrayData.Size()) : 0;
}

struct Schema
{
	void Error(const String& message)
	{
		LogError(LC_ASSET, "{}: {}", FileName, message);
		bOk = false;
	}

	const ConfigEntry* Get(const ConfigEntry& table, const String& table_path, const char* key, bool required)
	{
		const ConfigEntry* entry = nullptr;

		if (table.Members.IsInited()) {
			entry = table.GetMember(HashStr32(key));
		}

		if (entry == nullptr && required) {
			Error(String::Fmt("'{}' is missing required entry '{}'", table_path, key));
		}

		return entry;
	}

	const ConfigEntry* GetTable(const ConfigEntry& table, const String& table_path, const char* key, bool required)
	{
		const ConfigEntry* entry = Get(table, table_path, key, required);

		if (entry != nullptr && !IsTable(*entry)) {
			Error(String::Fmt("'{}.{}' must be a table", table_path, key));
			return nullptr;
		}

		return entry;
	}

	const ConfigEntry* GetString(const ConfigEntry& table, const String& table_path, const char* key, bool required)
	{
		const ConfigEntry* entry = Get(table, table_path, key, required);

		if (entry != nullptr && !IsString(*entry)) {
			Error(String::Fmt("'{}.{}' must be a string", table_path, key));
			return nullptr;
		}

		return entry;
	}

	void ReadString(const ConfigEntry& table, const String& table_path, const char* key, String& out,
					bool required = false)
	{
		if (const ConfigEntry* entry = GetString(table, table_path, key, required)) {
			out = entry->Get<const char*>();
		}
	}

	void ReadFloat(const ConfigEntry& table, const String& table_path, const char* key, float32& out)
	{
		const ConfigEntry* entry = Get(table, table_path, key, false);

		if (entry == nullptr) {
			return;
		}

		if (!IsNumber(*entry)) {
			Error(std::format("'{}.{}' must be a number", table_path, key));
			return;
		}

		out = entry->Get<float32>();
	}

	void ReadBool(const ConfigEntry& table, const String& table_path, const char* key, bool& out)
	{
		const ConfigEntry* entry = Get(table, table_path, key, false);

		if (entry == nullptr) {
			return;
		}

		if (!IsBool(*entry)) {
			Error(String::Fmt("'{}.{}' must be true or false", table_path, key));
			return;
		}

		out = entry->Get<int64>() != 0;
	}

	bool ReadFloats(const ConfigEntry& entry, const std::string& entry_path, float32* out, uint32 count)
	{
		// Validate that we are looking at an array with the proper number of values
		bool is_valid = (IsValueArray(entry) && GetArraySize(entry) == count);

		for (uint32 i = 0; is_valid && i < count; i++) {
			is_valid = IsNumber(entry.ArrayData[i]);
		}

		// There are invalid values, report it
		if (!is_valid) {
			Error(std::format("'{}' must be an array of {} numbers", entry_path, count));
			return false;
		}

		for (uint32 i = 0; i < count; i++) {
			out[i] = entry.ArrayData[i].Get<float32>();
		}

		return true;
	}

	bool CheckPath(const ConfigEntry& entry, const String& entry_path, String& out)
	{
		const String path = String(entry.Get<const char*>());

		if (path.IsEmpty() || path[0] == '/' || path.FindFirst('\\') != String::scNotFound ||
			path.FindFirst(':') != String::scNotFound) {
			LogError(LC_ASSET, "RxModel: '{}' must be a relative path with forward slashes, found '{}'", entry_path,
					 path);
			return false;
		}

		out = path;
		return true;
	}

	void ReadFileRef(const ConfigEntry& table, const String& table_path, RxFileRef& out)
	{
		if (const ConfigEntry* file = GetString(table, table_path, "file", true)) {
			CheckPath(*file, table_path + ".file", out.Path);
		}

		const ConfigEntry* hash = GetString(table, table_path, "hash", false);

		if (hash == nullptr) {
			return;
		}

		const std::string text = hash->Get<const char*>();
		bool is_valid = (text.size() == 16);

		for (size_t i = 0; is_valid && i < text.size(); i++) {
			is_valid = std::isxdigit(static_cast<unsigned char>(text[i])) != 0;
		}

		if (!is_valid) {
			Error(std::format("'{}.hash' must be 16 hexadecimal digits, found \"{}\"", table_path, text));
			return;
		}

		out.Hash = std::strtoull(text.c_str(), nullptr, 16);
		out.bHasHash = true;
	}

	void ReadTransform(const ConfigEntry& table, const std::string& table_path, RxTransform& out)
	{
		if (const ConfigEntry* position = Get(table, table_path, "pos", false)) {
			float32 values[3];

			if (ReadFloats(*position, table_path + ".pos", values, 3)) {
				out.Position = Vec3f(values[0], values[1], values[2]);
			}
		}

		if (const ConfigEntry* rotation = Get(table, table_path, "rotquat", false)) {
			float32 values[4];

			if (ReadFloats(*rotation, table_path + ".rotquat", values, 4)) {
				const float32 length = std::sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2] +
												 values[3] * values[3]);

				if (length < 1e-6f) {
					Error(std::format("'{}.rotquat' has zero length", table_path));
				}
				else {
					out.Rotation = Quat(values[0] / length, values[1] / length, values[2] / length, values[3] / length);
				}
			}
		}

		if (const ConfigEntry* scale = Get(table, table_path, "scale", false)) {
			if (IsNumber(*scale)) {
				const float32 uniform = scale->Get<float32>();
				out.Scale = Vec3f(uniform, uniform, uniform);
			}
			else {
				float32 values[3];

				if (ReadFloats(*scale, table_path + ".scale", values, 3)) {
					out.Scale = Vec3f(values[0], values[1], values[2]);
				}
			}
		}

		const bool has_rotation = std::abs(out.Rotation.GetX()) > 1e-7f || std::abs(out.Rotation.GetY()) > 1e-7f ||
								  std::abs(out.Rotation.GetZ()) > 1e-7f;

		out.bIsIdentity = !has_rotation && out.Position.X == 0.0f && out.Position.Y == 0.0f && out.Position.Z == 0.0f &&
						  out.Scale.X == 1.0f && out.Scale.Y == 1.0f && out.Scale.Z == 1.0f;
	}

	void ReadMaterial(const ConfigEntry& table, const String& table_path, RxMaterialDesc& out)
	{
		out.Name = table.Name.Get().CStr();

		ReadString(table, table_path, "shader", out.Shader, true);

		if (const ConfigEntry* textures = GetTable(table, table_path, "textures", false);
			textures != nullptr && textures->Members.IsInited()) {
			for (const ConfigEntry& texture : textures->Members) {
				const String slot = texture.Name.Get().CStr();
				const String texture_path = String::Fmt("{}.textures.{}", table_path, slot);

				String path;

				if (!IsString(texture)) {
					Error(std::format("'{}' must be a path", texture_path));
					continue;
				}

				if (!CheckPath(texture, texture_path, path)) {
					continue;
				}

				constexpr StringView extension = MakeStringView(".ktx2");
				bool is_ktx2 = path.Length > extension.Size;

				for (size_t i = 0; is_ktx2 && i < extension.Size; i++) {
					const char ch = path[path.Length - extension.Size + i];
					is_ktx2 = (std::tolower(static_cast<unsigned char>(ch)) == extension[i]);
				}

				if (!is_ktx2) {
					Error(String::Fmt("'{}' must be a .ktx2 file, found \"{}\"", texture_path, path));
					continue;
				}

				out.Textures.Emplace(slot, std::move(path));
			}
		}

		if (const ConfigEntry* params = GetTable(table, table_path, "params", false);
			params != nullptr && params->Members.IsInited()) {
			for (const ConfigEntry& value : params->Members) {
				RxMaterialParam param;
				param.Name = value.Name.Get().CStr();

				bool is_valid = true;

				if (IsNumber(value)) {
					param.Values.Insert(value.Get<float32>());
				}
				else {
					is_valid = IsValueArray(value);

					for (uint32 i = 0; is_valid && i < GetArraySize(value); i++) {
						is_valid = IsNumber(value.ArrayData[i]);

						if (is_valid) {
							param.Values.Insert(value.ArrayData[i].Get<float32>());
						}
					}
				}

				if (!is_valid) {
					Error(String::Fmt("'{}.params.{}' must be a float, int, bool or float array", table_path,
									  param.Name));
					continue;
				}

				out.Params.Emplace(std::move(param));
			}
		}

		if (const ConfigEntry* blend = GetString(table, table_path, "blend", false)) {
			const std::string mode = blend->Get<const char*>();

			if (mode == "opaque") {
				out.Blend = eRxBlendMode::Opaque;
			}
			else if (mode == "masked") {
				out.Blend = eRxBlendMode::Masked;
			}
			else if (mode == "blend") {
				out.Blend = eRxBlendMode::Blend;
			}
			else {
				Error(String::Fmt("'{}.blend' must be \"opaque\", \"masked\" or \"blend\", found \"{}\"", table_path,
								  mode));
			}
		}

		ReadFloat(table, table_path, "alpha_cutoff", out.AlphaCutoff);
		ReadBool(table, table_path, "double_sided", out.bDoubleSided);
	}

	void ReadClip(const ConfigEntry& table, const String& table_path, RxClipDesc& out)
	{
		out.Name = table.Name.Get().CStr();

		ReadFileRef(table, table_path, out.File);
		ReadBool(table, table_path, "loop", out.bLoop);
		ReadFloat(table, table_path, "speed", out.Speed);
		ReadBool(table, table_path, "root_motion", out.bRootMotion);
		ReadString(table, table_path, "root_joint", out.RootJoint);

		const ConfigEntry* events = Get(table, table_path, "events", false);

		if (events == nullptr) {
			return;
		}

		const String events_path = table_path + ".events";

		if (!events->bIsArray || (!events->IsObjectArray() && GetArraySize(*events) > 0)) {
			Error(std::format("'{}' must be an array of tables", events_path));
			return;
		}

		if (!events->Members.IsInited()) {
			return;
		}

		float32 previous_time = 0.0f;

		for (const ConfigEntry& event : events->Members) {
			const std::string event_path = std::format("{}[{}]", events_path, event.Name.Get().CStr());

			float32 time = -1.0f;
			ReadFloat(event, event_path, "time", time);

			if (Get(event, event_path, "time", true) == nullptr) {
				continue;
			}

			GetString(event, event_path, "name", true);
			GetTable(event, event_path, "params", false);

			if (time < previous_time) {
				Error(std::format("'{}' must be sorted by time", events_path));
			}

			previous_time = std::max(previous_time, time);
		}
	}

	template <typename TFunc>
	void ForEachTable(const ConfigFile& config, const char* key, TFunc&& func)
	{
		const ConfigEntry* table = config.GetEntry(HashStr32(key));

		if (table == nullptr) {
			return;
		}

		if (!IsTable(*table)) {
			Error(std::format("'{}' must be a table", key));
			return;
		}

		if (!table->Members.IsInited()) {
			return;
		}

		for (const ConfigEntry& member : table->Members) {
			const std::string member_path = std::format("{}.{}", key, member.Name.Get().CStr());

			if (!IsTable(member)) {
				Error(std::format("'{}' must be a table", member_path));
				continue;
			}

			func(member, member_path);
		}
	}

	const std::string FileName;
	bool bOk = true;
};

} // namespace

const RxMaterialParam* RxMaterialDesc::FindParam(const StringView name) const
{
	for (const RxMaterialParam& param : Params) {
		if (param.Name == name) {
			return &param;
		}
	}

	return nullptr;
}

const String* RxMaterialDesc::FindTexture(const StringView slot) const
{
	for (const auto& [name, path] : Textures) {
		if (name == slot) {
			return &path;
		}
	}

	return nullptr;
}

const RxMaterialDesc* RxModelDesc::FindMaterial(const StringView name) const
{
	for (const RxMaterialDesc& material : Materials) {
		if (material.Name == name) {
			return &material;
		}
	}

	return nullptr;
}

bool LoadRxModelDesc(const String& path, RxModelDesc& out_desc)
{
	ConfigFile config;
	config.Load(path.CStr());

	if (!config.GetEntries().IsInited()) {
		LogError(LC_ASSET, "Could not read model config '{}'", path);
		return false;
	}

	Schema schema { path.CStr() };

	if (config.HasErrors()) {
		schema.Error("Model config has syntax errors");
	}

	const ConfigEntry* format = config.GetEntry(HashStr32("format"));

	if (format == nullptr || !IsTable(*format)) {
		schema.Error("Missing required table 'format'");
	}
	else if (const ConfigEntry* version = schema.Get(*format, "format", "version", true)) {
		if (version->Type != eType::Int || version->bIsArray || version->Get<int64>() != 1) {
			schema.Error(String::Fmt("unsupported format version {}, expected 1", version->AsString().CStr()));
		}
	}

	const ConfigEntry* model = config.GetEntry(HashStr32("model"));

	if (model == nullptr || !IsTable(*model)) {
		schema.Error("missing required table 'model'");
	}
	else {
		const String file_name = path;

		const uint32 slash = file_name.FindLast('/');

		const size_t stem_start = (slash == String::scNotFound) ? 0 : slash + 1;
		const uint32 dot = file_name.FindLast('.');

		uint32 filename_start = String::scFullString;

		if (dot != String::scNotFound && dot > stem_start) {
			filename_start = (dot - stem_start);
		}

		out_desc.Name = file_name.SubStr(stem_start, filename_start);

		schema.ReadString(*model, "model", "name", out_desc.Name);

		if (const ConfigEntry* mesh = schema.GetTable(*model, "model", "mesh", true)) {
			schema.ReadFileRef(*mesh, "model.mesh", out_desc.Mesh);
		}

		if (const ConfigEntry* skeleton = schema.GetTable(*model, "model", "skeleton", false)) {
			schema.ReadFileRef(*skeleton, "model.skeleton", out_desc.Skeleton);
			out_desc.bHasSkeleton = true;
		}

		if (const ConfigEntry* root = schema.GetTable(*model, "model", "root", false)) {
			schema.ReadTransform(*root, "model.root", out_desc.Root);
		}

		out_desc.LodScreenSizes.Clear();
		out_desc.LodScreenSizes.Insert(1.0f);

		if (const ConfigEntry* sizes = schema.Get(*model, "model", "lod_screen_sizes", false)) {
			out_desc.LodScreenSizes.Clear();

			bool is_valid = IsValueArray(*sizes) && GetArraySize(*sizes) > 0;

			for (uint32 i = 0; is_valid && i < GetArraySize(*sizes); i++) {
				is_valid = IsNumber(sizes->ArrayData[i]);

				if (is_valid) {
					out_desc.LodScreenSizes.Insert(sizes->ArrayData[i].Get<float32>());
				}
			}

			if (!is_valid) {
				schema.Error("'model.lod_screen_sizes' must be an array of at least one float");
			}
		}
	}

	schema.ForEachTable(config, "materials", [&](const ConfigEntry& table, const std::string& table_path)
						{ schema.ReadMaterial(table, table_path, out_desc.Materials.Emplace()); });

	schema.ForEachTable(config, "sockets",
						[&](const ConfigEntry& table, const std::string& table_path)
						{
							RxSocketDesc& socket = out_desc.Sockets.Emplace();
							socket.Name = table.Name.Get().CStr();

							schema.ReadString(table, table_path, "joint", socket.Joint);
							schema.ReadTransform(table, table_path, socket.Transform);
						});

	schema.ForEachTable(config, "animations", [&](const ConfigEntry& table, const std::string& table_path)
						{ schema.ReadClip(table, table_path, out_desc.Clips.Emplace()); });

	return schema.bOk;
}

bool LoadRxWeaponDesc(const String& path, RxWeaponDesc& out_desc)
{
	ConfigFile config;
	config.Load(path.CStr());

	if (!config.GetEntries().IsInited()) {
		LogError(LC_ASSET, "Could not read weapon config '{}'", path);
		return false;
	}

	Schema schema { path.CStr() };

	if (config.HasErrors()) {
		schema.Error("Weapon config has syntax errors");
	}

	const ConfigEntry* format = config.GetEntry(HashStr32("format"));

	if (format == nullptr || !IsTable(*format)) {
		schema.Error("Missing required table 'format'");
	}
	else if (const ConfigEntry* version = schema.Get(*format, "format", "version", true)) {
		if (version->Type != eType::Int || version->bIsArray || version->Get<int64>() != 1) {
			schema.Error(String::Fmt("unsupported format version {}, expected 1", version->AsString().CStr()));
		}
	}

	const ConfigEntry* weapon = config.GetEntry(HashStr32("weapon"));

	if (weapon == nullptr || !IsTable(*weapon)) {
		schema.Error("missing required table 'weapon'");
		return false;
	}

	if (const ConfigEntry* model = schema.GetTable(*weapon, "weapon", "model", true)) {
		schema.ReadFileRef(*model, "weapon.model", out_desc.Model);
	}

	if (const ConfigEntry* viewhands = schema.GetTable(*weapon, "weapon", "viewhands", true)) {
		schema.ReadFileRef(*viewhands, "weapon.viewhands", out_desc.Viewhands);
	}

	if (const ConfigEntry* attach = schema.GetTable(*weapon, "weapon", "attach", true)) {
		schema.ReadString(*attach, "weapon.attach", "joint", out_desc.AttachJoint, true);
		schema.ReadString(*attach, "weapon.attach", "root", out_desc.RootJoint, true);
	}

	if (const ConfigEntry* skeleton = schema.GetTable(*weapon, "weapon", "skeleton", true)) {
		schema.ReadFileRef(*skeleton, "weapon.skeleton", out_desc.Skeleton);

		const auto read_count = [&](const char* key, uint32& out)
		{
			const ConfigEntry* entry = schema.Get(*skeleton, "weapon.skeleton", key, true);

			if (entry == nullptr) {
				return;
			}

			if (entry->Type != eType::Int || entry->bIsArray || entry->Get<int64>() <= 0) {
				schema.Error(std::format("'weapon.skeleton.{}' must be a positive integer", key));
				return;
			}

			out = static_cast<uint32>(entry->Get<int64>());
		};

		read_count("viewhands_joints", out_desc.ViewhandsJoints);
		read_count("weapon_joints", out_desc.WeaponJoints);
	}

	schema.ForEachTable(config, "animations", [&](const ConfigEntry& table, const std::string& table_path)
						{ schema.ReadClip(table, table_path, out_desc.Clips.Emplace()); });

	return schema.bOk;
}

} // namespace loader

} // namespace fx
