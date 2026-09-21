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
#include <string>


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

void ConfigFile::Load(const std::string& path)
{
	const std::string resolved_path = FilesystemIO::ResolvePath(path);

	File file(resolved_path.c_str(), File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		return;
	}

	InitConstants();

	Slice<char> file_buffer = file.Read<char>();

	if (file_buffer.pData == nullptr || file_buffer.Size == 0) {
		LogWarning(LC_CORE, "Config '{}' is empty or could not be read", path);
		if (file_buffer.pData) {
			gEnginePool->Free(file_buffer.pData);
		}
		return;
	}

	mTokenIndex = 0;
	mbHasErrors = false;
	mDepth = 0;

	Tokenizer tokenizer(file_buffer.pData, file_buffer.Size);
	tokenizer.SetFileExtension(".conf");
	tokenizer.IncludeFile(FilesystemIO::ResolvePath("Config/Internal/Constants.conf").c_str());
	tokenizer.Tokenize();

	Parse(tokenizer.GetTokens());

	gEnginePool->Free(file_buffer.pData);
}

static ConfigEntry::ePrimitiveType GetValueTokenType(const Token& token)
{
	using VType = ConfigEntry::ePrimitiveType;

	VType current_type = VType::None;

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

bool ConfigFile::ParseReference(ConfigPrimitive& value)
{
	Token* ident_token = GetToken();
	if (!EatToken(eTokenType::Identifier)) {
		return false;
	}

	ConfigEntry* value_entry = GetEntry(ident_token->GetHash());

	// If there is a dot following, search for a nested member
	while (value_entry != nullptr) {
		if (GetToken()->Type != eTokenType::Dot) {
			break;
		}

		NextToken();

		ident_token = GetToken();
		if (!EatToken(eTokenType::Identifier)) {
			break;
		}

		value_entry = value_entry->GetMember(ident_token->GetHash());
	}

	if (!value_entry) {
		LogError(LC_CORE, "Config({}): could not resolve reference", mTokenIndex);
		mbHasErrors = true;
		return false;
	}

	value = *value_entry;
	return true;
}

bool ConfigFile::ParseValue(ConfigPrimitive& value)
{
	using VType = ConfigEntry::ePrimitiveType;

	Token* value_token = GetToken();

	if (value_token->Type == eTokenType::Dollar) {
		EatToken(eTokenType::Dollar);

		return ParseReference(value);
	}

	// Check for constants
	if (value_token->Type == eTokenType::Identifier) {
		for (const ConfigEntry& entry : mConstants) {
			if (entry.Name == value_token->GetHash()) {
				value.Set(entry);
				NextToken();
				return true;
			}
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
		value.Set(value_token->GetStr());
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
		if (!EatToken(eTokenType::RBrace)) {
			return false;
		}

		return true;
	}

	// Parse array
	else if (GetToken()->Type == eTokenType::LBracket) {
		EatToken(eTokenType::LBracket);

		entry.bIsArray = true;

		Token* value_token = GetToken();
		entry.Type = GetValueTokenType(*value_token);

		while (!IsAtEnd() && GetToken()->Type != eTokenType::RBracket) {
			ConfigPrimitive value;
			if (!ParseValue(value)) {
				return false;
			}
			entry.AppendValue(std::move(value));

			if (GetToken()->Type == eTokenType::RBracket) {
				break;
			}

			if (!EatToken(eTokenType::Comma)) {
				return false;
			}
		}

		return EatToken(eTokenType::RBracket);
	}

	// Parse single value entry
	// [IDENTIFIER] = [INT | FLOAT | STRING]

	return ParseValue(entry);
}

void ConfigFile::Parse(PagedArray<Token>& tokens)
{
	if (!mConfigEntries.IsInited()) {
		mConfigEntries.Create(32);
	}

	mpTokens = &tokens;

	while (!IsAtEnd()) {
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

	if (mbHasErrors) {
		LogWarning(LC_CORE, "Config file had errors; malformed entries were skipped");
	}

	mpTokens = nullptr;
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


void ConfigFile::Write(const std::string& path)
{
	File file(path.c_str(), File::eModType::Write, File::eDataType::Text);

	if (!file.IsFileOpen()) {
		return;
	}

	for (const ConfigEntry& entry : mConfigEntries) {
		file.WriteMulti(entry.Name.Get(), " = ", entry.AsString(), '\n');
	}

	file.Close();
}

void ConfigFile::InitConstants()
{
	constexpr uint32 cMaxConstants = 16;

	mConstants.InitCapacity(cMaxConstants);

	// mConstants.Insert(ConfigEntry("TRUE", 1));
	// mConstants.Insert(ConfigEntry("FALSE", 0));

	// mConstants.Insert(ConfigEntry("OBJLAYER_WORLD", 0));
	// mConstants.Insert(ConfigEntry("OBJLAYER_PLAYER", 1));

	// mConstants.Insert(ConfigEntry("PHYS_STATIC", 0));
	// mConstants.Insert(ConfigEntry("PHYS_DYNAMIC", 1));
}

} // namespace fx
