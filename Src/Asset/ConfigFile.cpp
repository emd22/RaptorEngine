#include "ConfigFile.hpp"

#include <Color.hpp>
#include <Core/File.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>
#include <Util/Tokenizer.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_set>


namespace fx {

namespace {

std::string FormatFloat(float32 value)
{
	if (!std::isfinite(value)) {
		LogError(LC_CORE, "Config: {} cannot be written to a config file, writing 0.0 instead", value);
		return "0.000000";
	}

	char fixed[512];
	snprintf(fixed, sizeof(fixed), "%.6f", static_cast<double>(value));

	if (strtof(fixed, nullptr) == value) {
		return fixed;
	}

	char shortest[512];
	const auto result = std::to_chars(shortest, shortest + sizeof(shortest), value, std::chars_format::fixed);
	std::string text(shortest, result.ptr);

	if (text.find('.') == std::string::npos) {
		text += ".0";
	}

	return text;
}

std::string EscapeString(const std::string& text)
{
	std::string out;
	out.reserve(text.size());

	for (const char ch : text) {
		if (ch == '"' || ch == '\\') {
			out += '\\';
		}
		out += ch;
	}

	return out;
}

std::string UnescapeString(const std::string& text)
{
	std::string out;
	out.reserve(text.size());

	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) {
			++i;
		}
		out += text[i];
	}

	return out;
}

bool IsValidNameChar(char ch)
{
	static constexpr std::string_view cNameBreakers = "=()[]{}<>+-*/$.,;:?!&\"#\\";
	return ch != 0 && !std::isspace(static_cast<unsigned char>(ch)) && cNameBreakers.find(ch) == std::string_view::npos;
}

String ValidName(const String& name)
{
	if (!name.IsEmpty() && std::all_of(name.begin(), name.end(), IsValidNameChar)) {
		return name;
	}

	const String fixed = ConfigFile::MakeValidName(name);

	LogWarning(LC_CORE, "Config: '{}' is not a valid entry name, writing '{}' instead", name, fixed);
	return fixed;
}

} // namespace

String ConfigFile::MakeValidName(const String& name)
{
	String fixed = name;
	for (char& ch : fixed) {
		if (!IsValidNameChar(ch)) {
			ch = '_';
		}
	}

	if (fixed.IsEmpty()) {
		fixed = "_";
	}

	return fixed;
}

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

void ConfigEntry::CopyValueFrom(const ConfigEntry& other)
{
	if (this == &other) {
		return;
	}

	ConfigPrimitive::Set(static_cast<const ConfigPrimitive&>(other));

	bIsArray = other.bIsArray;
	bIsDotReference = other.bIsDotReference;

	if (Members.IsInited()) {
		Members.Destroy();
	}

	if (ArrayData.IsInited()) {
		ArrayData.Destroy();
	}

	if (other.ArrayData.IsInited()) {
		ArrayData.Create(8);

		for (const ConfigPrimitive& value : other.ArrayData) {
			ArrayData.Insert(ConfigPrimitive(value));
		}
	}

	if (other.Members.IsInited()) {
		Members.Create();

		for (const ConfigEntry& member : other.Members) {
			ConfigEntry copy;
			copy.Name = member.Name;
			copy.CopyValueFrom(member);

			Members.Insert(std::move(copy));
		}
	}
}

String ConfigPrimitive::AsString() const
{
	switch (Type) {
	case ePrimitiveType::None:
		return "";
	case ePrimitiveType::Int:
		return std::to_string(mIntValue);
	case ePrimitiveType::Float:
		return FormatFloat(mFloatValue);
	case ePrimitiveType::String:
		return std::format("\"{}\"", EscapeString(mStringValue ? mStringValue : ""));
	case ePrimitiveType::Struct:
		break;
	}

	return "";
}

String ConfigEntry::AsString(uint32 indent) const
{
	std::string member_list = "";

	std::string indent_str = "";

	for (uint32 i = 0; i < indent; i++) {
		indent_str += '\t';
	}


	if (IsObjectArray()) {
		for (const ConfigEntry& entry : Members) {
			member_list += std::format("{}\t{},\n", indent_str, entry.AsString(indent + 1));
		}

		return std::format("[\n{}{}]", member_list, indent_str);
	}
	else if (Type == ePrimitiveType::Struct) {
		for (const ConfigEntry& entry : Members) {
			member_list += std::format("{}\t{} = {}\n", indent_str, ValidName(entry.Name.Get()),
									   entry.AsString(indent + 1));
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
	for (const ConfigEntry& entry : Members) {
		if (entry.Name == name_hash) {
			return const_cast<ConfigEntry*>(&entry);
		}
	}

	return nullptr;
}


void ConfigEntry::AppendValue(const Vec3f vec)
{
	if (!ArrayData.IsInited()) {
		ArrayData.Create(4);
	}

	AppendValue(ConfigPrimitive::FromValue(vec.X));
	AppendValue(ConfigPrimitive::FromValue(vec.Y));
	AppendValue(ConfigPrimitive::FromValue(vec.Z));
}

void ConfigEntry::AppendValue(const Vec4f vec)
{
	if (!ArrayData.IsInited()) {
		ArrayData.Create(5);
	}

	AppendValue(ConfigPrimitive::FromValue(vec.X));
	AppendValue(ConfigPrimitive::FromValue(vec.Y));
	AppendValue(ConfigPrimitive::FromValue(vec.Z));
	AppendValue(ConfigPrimitive::FromValue(vec.W));
}


void ConfigEntry::AppendValue(const Quat quat)
{
	if (!ArrayData.IsInited()) {
		ArrayData.Create(5);
	}

	AppendValue(ConfigPrimitive::FromValue(quat.X));
	AppendValue(ConfigPrimitive::FromValue(quat.Y));
	AppendValue(ConfigPrimitive::FromValue(quat.Z));
	AppendValue(ConfigPrimitive::FromValue(quat.W));
}

ConfigEntry ConfigEntry::Clone(const String& clone_name) const
{
	ConfigEntry clone(clone_name);
	clone.CopyValueFrom(*this);
	return clone;
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

void ConfigFile::Load(const std::string& path)
{
	const String resolved_path = FilesystemIO::ResolvePath(path);

	File file(resolved_path.CStr(), File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		return;
	}

	InitConstants();
	mbLoadedConstants = true;

	SizedArray<char> file_buffer = file.Read2<char>();

	if (file_buffer.pData == nullptr || file_buffer.Size == 0) {
		LogWarning(LC_CORE, "Config '{}' is empty or could not be read", path);
		return;
	}

	mTokenIndex = 0;
	mbHasErrors = false;
	mDepth = 0;

	Tokenizer tokenizer(file_buffer.pData, file_buffer.Size);
	tokenizer.SetFileExtension(".conf");
	tokenizer.IncludeFile(FilesystemIO::ResolvePath("Config/Internal/Constants.conf").CStr());
	tokenizer.Tokenize();

	Parse(tokenizer.GetTokens());
}

static ConfigEntry::ePrimitiveType GetValueTokenType(const Token& token)
{
	using VType = ConfigEntry::ePrimitiveType;

	VType current_type = VType::None;

	if (token.Type == eTokenType::String) {
		return VType::String;
	}

	Token::eIsNumericResult numeric_result = token.IsNumeric();
	if (numeric_result != Token::eIsNumericResult::NaN) {
		if (numeric_result == Token::eIsNumericResult::Integer) {
			return VType::Int;
		}
		else {
			return VType::Float;
		}
	}

	if (token.Type == eTokenType::String) {
		current_type = VType::String;
	}

	return current_type;
}

bool ConfigFile::EatToken(eTokenType type)
{
	const bool correct_token = (GetToken()->Type == type);

	if (!correct_token) {
		LogError(LC_CORE, "Config({}): Expected '{}' but found '{}'", mTokenIndex, Token::GetTypeName(type),
				 Token::GetTypeName(GetToken()->Type));

		mbHasErrors = true;
		return false;
	}

	NextToken();

	return true;
}

bool ConfigFile::EatToken(const Slice<eTokenType>& expected_types)
{
	bool type_is_correct = false;

	Token* token = GetToken();

	for (const eTokenType type : expected_types) {
		if (token->Type == type) {
			NextToken();
			return true;
		}
	}

	LogError(LC_CORE, "Config({}): unexpected token type '{}'", mTokenIndex, Token::GetTypeName(token->Type));
	mbHasErrors = true;
	return false;
}

void ConfigFile::PrintEntries()
{
	PagedArray<ConfigEntry>& entries = GetEntries();

	for (const ConfigEntry& entry : entries) {
		LogInfo(LC_CORE, "Entry: {} -> {}", entry.Name.Get(), entry.AsString());
	}
}

bool ConfigFile::ScanReferencePath(std::string& out_path, uint32& out_end_index) const
{
	uint32 offset = 1;

	if (PeekToken(offset)->Type != eTokenType::Identifier) {
		return false;
	}

	out_path = PeekToken(offset)->GetStr();
	++offset;

	while (true) {
		const eTokenType type = PeekToken(offset)->Type;
		const eTokenType next_type = PeekToken(offset + 1)->Type;

		if (type == eTokenType::Dot && (next_type == eTokenType::Identifier || next_type == eTokenType::Integer)) {
			out_path += '.';
			out_path += PeekToken(offset + 1)->GetStr();
			offset += 2;
		}
		else if (type == eTokenType::LBracket && next_type == eTokenType::Integer &&
				 PeekToken(offset + 2)->Type == eTokenType::RBracket) {
			out_path += '[';
			out_path += PeekToken(offset + 1)->GetStr();
			out_path += ']';
			offset += 3;
		}
		else {
			break;
		}
	}

	out_end_index = mTokenIndex + offset;

	return true;
}

ConfigEntry* ConfigFile::WalkReferencePath(const std::string& path, const ConfigPrimitive*& out_primitive,
										   std::vector<ConfigEntry*>& resolving, bool report_failures)
{
	out_primitive = nullptr;

	ConfigEntry* current = nullptr;
	size_t position = 0;

	while (position < path.size()) {
		if (path[position] == '[') {
			const size_t close = path.find(']', position);
			const uint64 index = std::strtoull(path.c_str() + position + 1, nullptr, 10);

			position = close + 1;

			if (current->IsObjectArray()) {
				if (index >= current->Members.Size()) {
					return nullptr;
				}

				current = &current->Members[index];
			}
			else {
				const bool is_last_step = (position >= path.size());

				if (!current->bIsArray || !current->ArrayData.IsInited() || index >= current->ArrayData.Size() ||
					!is_last_step) {
					return nullptr;
				}

				out_primitive = &current->ArrayData[index];
				return current;
			}
		}
		else {
			if (path[position] == '.') {
				++position;
			}

			const size_t end = std::min(path.find_first_of(".[", position), path.size());
			const Hash32 name_hash = HashStr32(path.substr(position, end - position).c_str());

			current = (current == nullptr) ? GetEntry(name_hash) : current->GetMember(name_hash);
			position = end;
		}

		if (current == nullptr) {
			return nullptr;
		}

		if (current->bIsDotReference && !ResolveEntryReferences(*current, resolving, report_failures)) {
			return nullptr;
		}
	}

	return current;
}

bool ConfigFile::ResolveEntryReferences(ConfigEntry& entry, std::vector<ConfigEntry*>& resolving, bool report_failures)
{
	if (!entry.bIsDotReference) {
		bool resolved = true;

		if (entry.Members.IsInited()) {
			for (ConfigEntry& member : entry.Members) {
				resolved &= ResolveEntryReferences(member, resolving, report_failures);
			}
		}

		return resolved;
	}

	if (std::find(resolving.begin(), resolving.end(), &entry) != resolving.end()) {
		if (report_failures) {
			LogError(LC_CORE, "Config: reference '{}' is part of a cycle", entry.Get<const char*>());
			mbHasErrors = true;
		}
		return false;
	}

	const std::string path = entry.Get<const char*>() + 1;

	resolving.push_back(&entry);

	const ConfigPrimitive* primitive = nullptr;
	ConfigEntry* target = WalkReferencePath(path, primitive, resolving, report_failures);

	const bool resolved = target != nullptr &&
						  (primitive != nullptr || ResolveEntryReferences(*target, resolving, report_failures));

	resolving.pop_back();

	if (resolved) {
		entry.bIsDotReference = false;

		if (primitive != nullptr) {
			entry.ConfigPrimitive::Set(ConfigPrimitive(*primitive));
		}
		else {
			entry.CopyValueFrom(*target);
		}

		return true;
	}

	if (target == nullptr && path.find_first_of(".[") == std::string::npos) {
		if (const ConfigEntry* constant = FindConstant(HashStr32(path.c_str()))) {
			entry.bIsDotReference = false;
			entry.ConfigPrimitive::Set(static_cast<const ConfigPrimitive&>(*constant));
			return true;
		}
	}

	if (report_failures) {
		LogError(LC_CORE, "Config: could not resolve reference '${}' assigned to '{}'", path, entry.Name.Get());
		mbHasErrors = true;

		entry.bIsDotReference = false;
		entry.ConfigPrimitive::Set(ConfigPrimitive());
	}

	return false;
}

bool ConfigFile::ParseReference(ConfigPrimitive& value)
{
	std::string path;
	uint32 end_index = 0;

	if (!ScanReferencePath(path, end_index)) {
		NextToken();
		return EatToken(eTokenType::Identifier);
	}

	mTokenIndex = end_index;

	std::vector<ConfigEntry*> resolving;

	const ConfigPrimitive* primitive = nullptr;
	const ConfigEntry* value_entry = WalkReferencePath(path, primitive, resolving, false);

	if (value_entry == nullptr && path.find_first_of(".[") == std::string::npos) {
		value_entry = FindConstant(HashStr32(path.c_str()));
	}

	if (value_entry == nullptr) {
		LogError(LC_CORE, "Config({}): could not resolve reference '${}'", mTokenIndex, path);
		mbHasErrors = true;
		return false;
	}

	value.Set((primitive != nullptr) ? *primitive : static_cast<const ConfigPrimitive&>(*value_entry));
	return true;
}

bool ConfigFile::ParseValue(ConfigPrimitive& value)
{
	using VType = ConfigEntry::ePrimitiveType;

	Token* value_token = GetToken();

	if (value_token->Type == eTokenType::Dollar) {
		return ParseReference(value);
	}

	// Check for constants
	if (value_token->Type == eTokenType::Identifier) {
		if (const ConfigEntry* constant = FindConstant(value_token->GetHash())) {
			value.Set(*constant);
			NextToken();
			return true;
		}

		LogError(LC_CORE, "Could not find reference to constant {}!", value_token->GetStr());
		mbHasErrors = true;
	}

	// Handle unary minus (in a simple way, but still handles recursive negatives)
	if (value_token->Type == eTokenType::Minus) {
		EatToken(eTokenType::Minus);

		if (mDepth >= cMaxDepth) {
			LogError(LC_CORE, "Config({}): nesting too deep", mTokenIndex);
			mbHasErrors = true;
			return false;
		}

		ConfigPrimitive temp;
		++mDepth;
		const bool ok = ParseValue(temp);
		--mDepth;

		if (!ok) {
			return false;
		}

		switch (temp.Type) {
		case VType::Int:
			value.Set<int64>(-temp.Get<int64>());
			break;
		case VType::Float:
			value.Set<float32>(-temp.Get<float32>());
			break;
		default:
			LogError(LC_CORE, "Config({}): cannot negate a non-numeric value", mTokenIndex);
			mbHasErrors = true;
			return false;
		}

		return true;
	}

	const VType detected_type = GetValueTokenType(*value_token);

	switch (detected_type) {
	case VType::None:
		LogError(LC_CORE, "Config({}): expected a value but found '{}'", mTokenIndex,
				 Token::GetTypeName(value_token->Type));
		mbHasErrors = true;
		// Do not consume closing tokens, so the caller can resynchronise on them
		if (value_token->Type != eTokenType::Unknown && value_token->Type != eTokenType::RBrace &&
			value_token->Type != eTokenType::RBracket) {
			NextToken();
		}
		return false;
	case VType::String:
		value.Set(UnescapeString(value_token->GetStr()));
		break;
	case VType::Int:
		value.Set<int64>(value_token->ToInt());
		break;
	case VType::Float:
		value.Set<float32>(value_token->ToFloat());
		break;
	default:
		break;
	}

	NextToken();
	return true;
}

bool ConfigFile::ParseTerm(ConfigPrimitive& value)
{
	if (GetToken()->Type != eTokenType::LParen) {
		return ParseValue(value);
	}

	if (mDepth >= cMaxDepth) {
		LogError(LC_CORE, "Config({}): nesting too deep", mTokenIndex);
		mbHasErrors = true;
		return false;
	}

	EatToken(eTokenType::LParen);

	++mDepth;
	const bool ok = ParseExpression(value);
	--mDepth;

	if (!ok) {
		return false;
	}

	return EatToken(eTokenType::RParen);
}

bool ConfigFile::ParseExpression(ConfigPrimitive& value)
{
	using VType = ConfigEntry::ePrimitiveType;

	if (!ParseTerm(value)) {
		return false;
	}

	while (GetToken()->Type == eTokenType::Pipe) {
		EatToken(eTokenType::Pipe);

		ConfigPrimitive rhs;
		if (!ParseTerm(rhs)) {
			return false;
		}

		if (value.Type != VType::Int || rhs.Type != VType::Int) {
			LogError(LC_CORE, "Config({}): '|' can only combine integer values", mTokenIndex);
			mbHasErrors = true;
			return false;
		}

		value.Set<int64>(value.Get<int64>() | rhs.Get<int64>());
	}

	return true;
}

void ConfigEntry::AppendValue(ConfigPrimitive&& value)
{
	if (!ArrayData.IsInited()) {
		ArrayData.Create(8);
	}

	ArrayData.Insert(std::move(value));
}

void ConfigFile::SkipToNextEntry(bool in_struct)
{
	while (!IsAtEnd()) {
		const Token* token = GetToken();

		if (in_struct && token->Type == eTokenType::RBrace) {
			return;
		}

		const bool can_start_entry = token->Type == eTokenType::Identifier || token->Type == eTokenType::Integer ||
									 token->Type == eTokenType::Dollar;

		if (can_start_entry && PeekToken(1)->Type == eTokenType::Equals) {
			return;
		}

		NextToken();
	}
}

bool ConfigFile::ParseEntry(ConfigEntry* parent, ConfigEntry& entry)
{
	// [IDENTIFIER] = [INT | FLOAT | STRING | STRUCT]

	Token* token = GetToken();

	// Special case, set the name to the current member index
	if (token->Type == eTokenType::Dollar && parent != nullptr) {
		entry.Name = std::to_string(parent->Members.IsInited() ? parent->Members.Size() : 0);
	}
	// Default case, set the name to the identifier
	else {
		entry.Name = token->GetStr();
	}

	eTokenType allowed_name_types[] = { eTokenType::Identifier, eTokenType::Integer, eTokenType::Dollar };
	if (!EatToken(MakeSlice(allowed_name_types, std::size(allowed_name_types)))) {
		return false;
	}
	if (!EatToken(eTokenType::Equals)) {
		return false;
	}

	// Parse struct
	// [IDENTIFIER] = { [ENTRY]... }
	if (GetToken()->Type == eTokenType::LBrace) {
		return ParseStruct(entry);
	}

	// Parse array
	else if (GetToken()->Type == eTokenType::LBracket) {
		EatToken(eTokenType::LBracket);

		entry.bIsArray = true;

		Token* value_token = GetToken();
		entry.Type = GetValueTokenType(*value_token);

		while (!IsAtEnd() && GetToken()->Type != eTokenType::RBracket) {
			if (GetToken()->Type == eTokenType::LBrace) {
				ConfigEntry element;
				element.Name = std::to_string(entry.Members.IsInited() ? entry.Members.Size() : 0);

				if (!ParseStruct(element)) {
					return false;
				}

				entry.AddMember(std::move(element));
			}
			else {
				ConfigPrimitive value;
				if (!ParseExpression(value)) {
					return false;
				}
				entry.AppendValue(std::move(value));
			}

			if (GetToken()->Type == eTokenType::RBracket) {
				break;
			}

			if (!EatToken(eTokenType::Comma)) {
				return false;
			}
		}

		return EatToken(eTokenType::RBracket);
	}

	// Parse a reference to another entry, which can be an object or an array and can be defined later in the file
	// [IDENTIFIER] = $[IDENTIFIER](.[IDENTIFIER] | \[[INT]\])...
	if (GetToken()->Type == eTokenType::Dollar) {
		std::string path;
		uint32 end_index = 0;

		if (ScanReferencePath(path, end_index) && PeekToken(end_index - mTokenIndex)->Type != eTokenType::Pipe) {
			mTokenIndex = end_index;

			entry.Set(String("$" + path));
			entry.bIsDotReference = true;

			mbHasEntryReferences = true;

			return true;
		}
	}

	// Parse single value entry
	// [IDENTIFIER] = [INT | FLOAT | STRING | ( EXPR | EXPR )]

	return ParseExpression(entry);
}

bool ConfigFile::ParseStruct(ConfigEntry& entry)
{
	if (mDepth >= cMaxDepth) {
		LogError(LC_CORE, "Config({}): nesting too deep", mTokenIndex);
		mbHasErrors = true;
		return false;
	}

	EatToken(eTokenType::LBrace);

	entry.Type = ConfigEntry::ePrimitiveType::Struct;

	++mDepth;

	// Add each entry as a member of the current entry
	while (!IsAtEnd() && GetToken()->Type != eTokenType::RBrace) {
		if (GetToken()->Type == eTokenType::Semicolon) {
			NextToken();
			continue;
		}

		const uint32 start_index = mTokenIndex;

		ConfigEntry member;
		if (ParseEntry(&entry, member)) {
			entry.AddMember(std::move(member));
			continue;
		}

		// Malformed member, drop it and resume at the next entry
		SkipToNextEntry(true);
		if (mTokenIndex == start_index && !IsAtEnd() && GetToken()->Type != eTokenType::RBrace) {
			NextToken();
		}
	}

	--mDepth;

	// Missing closing brace (truncated file). Keep what was parsed.
	return EatToken(eTokenType::RBrace);
}

void ConfigFile::Parse(PagedArray<Token>& tokens)
{
	if (!mConfigEntries.IsInited()) {
		mConfigEntries.Create(32);
	}

	mpTokens = &tokens;

	while (!IsAtEnd()) {
		if (GetToken()->Type == eTokenType::Semicolon) {
			NextToken();
			continue;
		}

		const uint32 start_index = mTokenIndex;

		ConfigEntry entry;
		if (ParseEntry(nullptr, entry)) {
			mConfigEntries.Insert(std::move(entry));
			continue;
		}

		// Malformed entry, skip it and resume at the next one
		SkipToNextEntry(false);
		if (mTokenIndex == start_index && !IsAtEnd()) {
			NextToken();
		}
	}

	if (mbHasEntryReferences) {
		std::vector<ConfigEntry*> resolving;

		for (ConfigEntry& entry : mConfigEntries) {
			ResolveEntryReferences(entry, resolving, true);
		}

		mbHasEntryReferences = false;
	}

	if (mbHasErrors) {
		LogWarning(LC_CORE, "Config file had errors; malformed entries were skipped");
	}

	mpTokens = nullptr;
}

ConfigEntry* ConfigFile::GetEntry(Hash32 requested_name_hash) const
{
	for (const ConfigEntry& entry : mConfigEntries) {
		if (entry.Name == requested_name_hash) {
			return const_cast<ConfigEntry*>(&entry);
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

bool ConfigFile::Write(const std::string& path)
{
	File file(path.c_str(), File::eModType::Write, File::eDataType::Text);

	if (!file.IsFileOpen()) {
		return false;
	}

	for (const ConfigEntry& entry : mConfigEntries) {
		if (mbLoadedConstants && IsConstantEntry(entry)) {
			continue;
		}

		file.WriteMulti(ValidName(entry.Name.Get()), " = ", entry.AsString(), '\n');
	}

	file.Close();

	return true;
}

void ConfigFile::InitConstants()
{
	constexpr uint32 cMaxConstants = 16;

	mConstants.InitCapacity(cMaxConstants);

	mConstants.Insert(ConfigEntry("true", int64(1)));
	mConstants.Insert(ConfigEntry("false", int64(0)));

	mConstants.Insert(ConfigEntry("True", int64(1)));
	mConstants.Insert(ConfigEntry("False", int64(0)));
}

const ConfigEntry* ConfigFile::FindConstant(Hash32 name_hash) const
{
	for (const ConfigEntry& entry : mConstants) {
		if (entry.Name == name_hash) {
			return &entry;
		}
	}

	return nullptr;
}

} // namespace fx
