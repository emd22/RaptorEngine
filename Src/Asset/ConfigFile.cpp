#include "ConfigFile.hpp"

#include <Color.hpp>
#include <Core/File.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Path.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>
#include <cstring>
#include <string>
#include <unordered_set>

// Includes for Rust stuff
#include <raptor_ffi.h>


namespace fx {

/////////////////////////////////////
// Config entry functions
/////////////////////////////////////


ConfigEntry& ConfigEntry::operator=(ConfigEntry&& other)
{
	// Free old string if we had one
	if (Type == ePrimitiveType::String && mStringValue) {
		free(mStringValue);
		mStringValue = nullptr;
	}

	if (other.Type == ePrimitiveType::String) {
		mStringValue = other.mStringValue;
		other.mStringValue = nullptr;
		Type = other.Type;
	}
	else if (other.Type == ePrimitiveType::Int) {
		Type = other.Type;
		mIntValue = other.mIntValue;
	}
	else if (other.Type == ePrimitiveType::Float) {
		Type = other.Type;
		mFloatValue = other.mFloatValue;
	}
	else {
		Type = other.Type;
		mStringValue = nullptr;
	}
	other.Type = ePrimitiveType::None;

	Members = std::move(other.Members);
	ArrayData = std::move(other.ArrayData);

	bIsArray = other.bIsArray;
	other.bIsArray = false;

	bIsDotReference = other.bIsDotReference;
	other.bIsDotReference = false;

	Name = other.Name;
	other.Name.Clear();

	return *this;
}

ConfigEntry::ConfigEntry(ConfigEntry&& other) { (*this) = std::move(other); }

void ConfigEntry::AddMember(ConfigEntry&& entry)
{
	if (!Members.IsInited()) {
		Members.Create();
	}

	Members.Insert(std::move(entry));

	if (Type == ePrimitiveType::String && mStringValue) {
		free(mStringValue);
		mStringValue = nullptr;
	}
	Type = ConfigEntry::ePrimitiveType::Struct;
}

std::string ConfigPrimitive::AsString() const
{
	switch (Type) {
	case ePrimitiveType::None:
		return "";
	case ePrimitiveType::Int:
		return std::to_string(mIntValue);
	case ePrimitiveType::Float:
		return std::to_string(mFloatValue);
	case ePrimitiveType::String:
		return std::format("\"{}\"", mStringValue);
	case ePrimitiveType::Struct:
		break;
	}

	return "";
}

std::string ConfigEntry::AsString(uint32 indent) const
{
	std::string member_list = "";

	std::string indent_str = "";

	for (uint32 i = 0; i < indent; i++) {
		indent_str += '\t';
	}


	if (Type == ePrimitiveType::Struct) {
		for (const ConfigEntry& entry : Members) {
			member_list += std::format("{}\t{} = {}\n", indent_str, entry.Name.Get(), entry.AsString(indent + 1));
		}

		return std::format("{{\n{}{}}}", member_list, indent_str);
	}
	else if (bIsArray) {
		uint32 array_size = ArrayData.Size();

		for (uint32 value_index = 0; value_index < array_size; value_index++) {
			const ConfigPrimitive& value = ArrayData[value_index];
			if (value_index == array_size - 1) {
				member_list += std::format("{}", value.AsString());
			}
			else {
				member_list += std::format("{}, ", value.AsString());
			}
		}

		return std::format("[ {} ]", member_list);
	}
	else if (bIsDotReference) {
		return std::format("{}", this->mStringValue);
	}

	return this->ConfigPrimitive::AsString();
}

ConfigEntry* ConfigEntry::GetMember(const Hash32 name_hash) const
{
	for (ConfigEntry& entry : Members) {
		if (entry.Name == name_hash) {
			return &entry;
		}
	}

	return nullptr;
}


void ConfigEntry::AppendValue(const Vec3f& vec)
{
	if (!ArrayData.IsInited()) {
		ArrayData.Create(4);
	}

	AppendValue(ConfigPrimitive::FromValue(vec.X));
	AppendValue(ConfigPrimitive::FromValue(vec.Y));
	AppendValue(ConfigPrimitive::FromValue(vec.Z));
}

void ConfigEntry::AppendValue(const Vec4f& vec)
{
	if (!ArrayData.IsInited()) {
		ArrayData.Create(5);
	}

	AppendValue(ConfigPrimitive::FromValue(vec.X));
	AppendValue(ConfigPrimitive::FromValue(vec.Y));
	AppendValue(ConfigPrimitive::FromValue(vec.Z));
	AppendValue(ConfigPrimitive::FromValue(vec.W));
}


void ConfigEntry::AppendValue(const Quat& quat)
{
	if (!ArrayData.IsInited()) {
		ArrayData.Create(5);
	}

	AppendValue(ConfigPrimitive::FromValue(quat.X));
	AppendValue(ConfigPrimitive::FromValue(quat.Y));
	AppendValue(ConfigPrimitive::FromValue(quat.Z));
	AppendValue(ConfigPrimitive::FromValue(quat.W));
}


ConfigEntry::~ConfigEntry()
{
	// NOTE: Do not clobber Type/mStringValue here. The ConfigPrimitive base
	// destructor runs after this body and is responsible for freeing the
	// string value. Nulling Type first would leak it.
	if (Members.IsInited()) {
		Members.Destroy();
	}
	if (ArrayData.IsInited()) {
		ArrayData.Destroy();
	}
}


/////////////////////////////////////
// Config file functions
/////////////////////////////////////

namespace {

int32 ReadIncludeForRust(void*, const char* path, const char* extension, uint8** data, size_t* length)
{
	Path vpath(path);

	if (!vpath.HasExtension()) {
		String* basename = vpath.BaseName();
		(*basename) += extension;
		LogInfo("basename : {}", *basename);
	}

	File file(vpath.Str(), File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		return 0;
	}

	Slice<char> content = file.Read<char>();

	*length = content.pData ? content.Size : 0;
	*data = new uint8[*length + 1];

	if (*length) {
		std::memcpy(*data, content.pData, *length);
	}

	if (content.pData) {
		gEnginePool->Free(content.pData);
	}

	return 1;
}

void ReleaseIncludeForRust(void*, uint8* data) { delete[] data; }

void LogForRust(void*, int32 level, int32 category, const char* message, size_t length)
{
	const std::string_view text(message, length);
	const eLogCategory log_category = static_cast<eLogCategory>(category);

	switch (level) {
	case RX_LOG_PRINT:
		puts(std::string(text).c_str());
		break;
	case RX_LOG_INFO:
		LogInfo(log_category, "{}", text);
		break;
	case RX_LOG_WARNING:
		LogWarning(log_category, "{}", text);
		break;
	default:
		LogError(log_category, "{}", text);
		break;
	}
}

void ReadPrimitiveFromRust(ConfigPrimitive& out, const RxPrimitive& in)
{
	switch (static_cast<RxValueKind>(in.kind)) {
	case RX_KIND_INT:
		out.Set<int64>(in.int_value);
		break;
	case RX_KIND_FLOAT:
		out.Set<float32>(in.float_value);
		break;
	case RX_KIND_STRING:
		if (in.string_value) {
			out.Set(std::string(in.string_value, in.string_length));
		}
		else {
			out.Type = ConfigPrimitive::ePrimitiveType::String;
			out.mStringValue = nullptr;
		}
		break;
	case RX_KIND_STRUCT:
		out.Type = ConfigPrimitive::ePrimitiveType::Struct;
		break;
	default:
		break;
	}
}

ConfigEntry ReadEntryFromRust(const RxEntry& in)
{
	ConfigEntry entry;
	entry.Name = std::string(in.name, in.name_length);
	ReadPrimitiveFromRust(entry, in.value);
	entry.bIsArray = in.is_array != 0;

	for (size_t i = 0; i < in.array_count; i++) {
		ConfigPrimitive value;
		ReadPrimitiveFromRust(value, in.array[i]);
		entry.AppendValue(std::move(value));
	}

	for (size_t i = 0; i < in.member_count; i++) {
		entry.AddMember(ReadEntryFromRust(in.members[i]));
	}

	return entry;
}

} // namespace

void ConfigFile::Load(const std::string& path)
{
	const std::string resolved_path = FilesystemIO::ResolvePath(path);

	File file(resolved_path.c_str(), File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		return;
	}

	mbLoadedConstants = true;

	Slice<char> file_buffer = file.Read<char>();

	if (file_buffer.pData == nullptr || file_buffer.Size == 0) {
		LogWarning(LC_CORE, "Config '{}' is empty or could not be read", path);
		if (file_buffer.pData) {
			gEnginePool->Free(file_buffer.pData);
		}
		return;
	}

	mbHasErrors = false;

	const RxHost host = {
		.user = nullptr,
		.read_include = ReadIncludeForRust,
		.release_include = ReleaseIncludeForRust,
		.log = LogForRust,
	};

	const std::string constants_path = FilesystemIO::ResolvePath("Config/Internal/Constants.conf");

	RxConfig* parsed = rx_config_parse(reinterpret_cast<const uint8*>(file_buffer.pData), file_buffer.Size,
									   constants_path.c_str(), ".conf", &host);

	gEnginePool->Free(file_buffer.pData);

	if (!mConfigEntries.IsInited()) {
		mConfigEntries.Create(32);
	}

	if (!parsed) {
		LogError(LC_CORE, "Config '{}' could not be parsed", path);
		mbHasErrors = true;
		return;
	}

	mbHasErrors = rx_config_has_errors(parsed) != 0;

	const RxEntry* entries = rx_config_entries(parsed);
	const size_t entry_count = rx_config_entry_count(parsed);

	for (size_t i = 0; i < entry_count; i++) {
		mConfigEntries.Insert(ReadEntryFromRust(entries[i]));
	}

	rx_config_free(parsed);
}

void ConfigFile::PrintEntries()
{
	PagedArray<ConfigEntry>& entries = GetEntries();

	for (const ConfigEntry& entry : entries) {
		LogInfo(LC_CORE, "Entry: {} -> {}", entry.Name.Get(), entry.AsString());
	}
}

void ConfigEntry::AppendValue(ConfigPrimitive&& value)
{
	if (!ArrayData.IsInited()) {
		ArrayData.Create(8);
	}

	ArrayData.Insert(std::move(value));
}

ConfigEntry* ConfigFile::GetEntry(Hash32 requested_name_hash) const
{
	for (ConfigEntry& entry : mConfigEntries) {
		if (entry.Name == requested_name_hash) {
			return &entry;
		}
	}

	return nullptr;
}


static bool IsConstantEntry(const ConfigEntry& entry)
{
	static const std::unordered_set<Hash32> constant_names = []
	{
		std::unordered_set<Hash32> names;

		ConfigFile constants;
		constants.Load("Config/Internal/Constants.conf");

		for (const ConfigEntry& constant : constants.GetEntries()) {
			names.insert(constant.Name.GetHash());
		}

		return names;
	}();

	return constant_names.contains(entry.Name.GetHash());
}

void ConfigFile::Write(const std::string& path)
{
	File file(path.c_str(), File::eModType::Write, File::eDataType::Text);

	if (!file.IsFileOpen()) {
		return;
	}

	for (const ConfigEntry& entry : mConfigEntries) {
		if (mbLoadedConstants && IsConstantEntry(entry)) {
			continue;
		}

		file.WriteMulti(entry.Name.Get(), " = ", entry.AsString(), '\n');
	}

	file.Close();
}

} // namespace fx
