#include "ConfigFile.hpp"

#include <Color.hpp>
#include <Core/File.hpp>
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
    if (other.Type == ConfigEntry::eValueType::String) {
        mStringValue = other.mStringValue;
        other.mStringValue = nullptr;
    }
    else {
        Set(other);
    }

    Type = other.Type;
    other.Type = ConfigEntry::eValueType::None;

    Members = std::move(other.Members);
    ArrayData = std::move(other.ArrayData);

    bIsArray = other.bIsArray;
    other.bIsArray = false;

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

    Type = ConfigEntry::eValueType::Struct;
}

std::string ConfigValue::AsString() const
{
    switch (Type) {
    case eValueType::None:
        return "";
    case eValueType::Int:
        return std::to_string(mIntValue);
    case eValueType::Float:
        return std::to_string(mFloatValue);
    case eValueType::String:
        return std::format("\"{}\"", mStringValue);
    case eValueType::Struct:
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


    if (Type == eValueType::Struct) {
        for (const ConfigEntry& entry : Members) {
            member_list += std::format("{}\t{} = {}\n", indent_str, entry.Name.Get(), entry.AsString(indent + 1));
        }

        return std::format("{{\n{}{}}}", member_list, indent_str);
    }
    else if (bIsArray) {
        uint32 array_size = ArrayData.Size();

        for (uint32 value_index = 0; value_index < array_size; value_index++) {
            const ConfigValue& value = ArrayData[value_index];
            if (value_index == array_size - 1) {
                member_list += std::format("{}", value.AsString());
            }
            else {
                member_list += std::format("{}, ", value.AsString());
            }
        }

        return std::format("[ {} ]", member_list);
    }


    return this->ConfigValue::AsString();
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
    if (!Members.IsInited()) {
        Members.Create(4);
    }

    AppendValue(vec.X);
    AppendValue(vec.Y);
    AppendValue(vec.Z);
}

void ConfigEntry::AppendValue(const Vec4f& vec)
{
    if (!Members.IsInited()) {
        Members.Create(5);
    }

    AppendValue(vec.X);
    AppendValue(vec.Y);
    AppendValue(vec.Z);
    AppendValue(vec.W);
}


void ConfigEntry::AppendValue(const Quat& quat)
{
    if (!Members.IsInited()) {
        Members.Create(5);
    }

    AppendValue(quat.X);
    AppendValue(quat.Y);
    AppendValue(quat.Z);
    AppendValue(quat.W);
}


ConfigEntry::~ConfigEntry()
{
    if (Type == eValueType::String && mStringValue != nullptr) {
        free(mStringValue);
    }

    Type = eValueType::None;

    mStringValue = nullptr;
    if (Members.IsInited()) {
        Members.Destroy();
    }
}


/////////////////////////////////////
// Config file functions
/////////////////////////////////////

void ConfigFile::Load(const std::string& path)
{
    File file(path.c_str(), File::eModType::Read, File::eDataType::Binary);

    if (!file.IsFileOpen()) {
        return;
    }

    InitConstants();

    Slice<char> file_buffer = file.Read<char>();

    Tokenizer tokenizer(file_buffer.pData, file_buffer.Size);
    tokenizer.Tokenize();

    Parse(tokenizer.GetTokens());

    gEnginePool->Free(file_buffer.pData);
}

static ConfigEntry::eValueType GetValueTokenType(const Token& token)
{
    using VType = ConfigEntry::eValueType;

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

    for (uint32 i = 0; i < token.Length; i++) {
        char ch = token.Start[i];

        if (ch == '"') {
            current_type = VType::String;
        }
    }

    return current_type;
}

bool ConfigFile::EatToken(eTokenType type)
{
    const bool correct_token = (GetToken()->Type == type);

    if (!correct_token) {
        LogError("Config: Expected '{}' but found '{}'", Token::GetTypeName(type),
                 Token::GetTypeName(GetToken()->Type));
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

    LogError("Config: found type '{}' but can only allow one of {}", Token::GetTypeName(GetToken()->Type));
    return false;
}

void ConfigFile::PrintEntries()
{
    PagedArray<ConfigEntry>& entries = GetEntries();

    for (const ConfigEntry& entry : entries) {
        LogInfo("Entry: {} -> {}", entry.Name.Get(), entry.AsString());
    }
}

void ConfigFile::ParseValue(ConfigValue& value)
{
    using VType = ConfigEntry::eValueType;

    Token* value_token = GetToken();

    // Check for constants
    if (value_token->Type == eTokenType::Identifier) {
        for (const ConfigEntry& entry : mConstants) {
            if (entry.Name == value_token->GetHash()) {
                value.Set(entry);
                NextToken();
                return;
            }
        }

        LogError("Could not find reference to constant {}!", value_token->GetStr());
    }

    // Handle unary minus (in a simple way, but still handles recursive negatives)
    if (value_token->Type == eTokenType::Minus) {
        EatToken(eTokenType::Minus);

        ConfigValue temp;
        ParseValue(temp);

        switch (temp.Type) {
        case VType::Int:
            value.Set<int64>(-temp.Get<int64>());
            break;
        case VType::Float:
            value.Set<float32>(-temp.Get<float32>());
            break;
        default:
            break;
        }

        return;
    }

    value.Type = GetValueTokenType(*value_token);


    switch (value.Type) {
    case VType::None:
        break;
    case VType::String:
        // Remove the first quote
        value_token->Start++;
        value_token->Length--;

        // Remove the ending quote
        value_token->Length--;
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
}

void ConfigEntry::AppendValue(ConfigValue&& value)
{
    if (!ArrayData.IsInited()) {
        ArrayData.Create(8);
    }

    ArrayData.Insert(std::move(value));
}

ConfigEntry ConfigFile::ParseEntry()
{
    // [IDENTIFIER] = [INT | FLOAT | STRING | STRUCT]

    ConfigEntry entry;

    Token* token = GetToken();

    entry.Name = token->GetStr();

    eTokenType allowed_name_types[] = { eTokenType::Identifier, eTokenType::Integer };
    EatToken(MakeSlice(allowed_name_types, std::size(allowed_name_types)));
    EatToken(eTokenType::Equals);

    // Parse struct
    // [IDENTIFIER] = { [ENTRY]... }
    if (GetToken()->Type == eTokenType::LBrace) {
        EatToken(eTokenType::LBrace);

        entry.Type = ConfigEntry::eValueType::Struct;

        // Add each entry as a member of the current entry
        while (GetToken()->Type != eTokenType::RBrace) {
            ConfigEntry member = ParseEntry();
            entry.AddMember(std::move(member));
        }

        EatToken(eTokenType::RBrace);

        return entry;
    }

    // Parse array
    else if (GetToken()->Type == eTokenType::LBracket) {
        EatToken(eTokenType::LBracket);

        entry.bIsArray = true;

        Token* value_token = GetToken();
        entry.Type = GetValueTokenType(*value_token);

        while (GetToken()->Type != eTokenType::RBracket) {
            ConfigValue value;
            value.Type = entry.Type;
            ParseValue(value);
            entry.AppendValue(std::move(value));

            if (GetToken()->Type == eTokenType::RBracket) {
                break;
            }

            EatToken(eTokenType::Comma);
        }

        EatToken(eTokenType::RBracket);

        return entry;
    }

    // Parse single value entry
    // [IDENTIFIER] = [INT | FLOAT | STRING]

    Token* value_token = GetToken();
    entry.Type = GetValueTokenType(*value_token);
    ParseValue(entry);

    return entry;
}

void ConfigFile::Parse(PagedArray<Token>& tokens)
{
    mConfigEntries.Create(32);

    mpTokens = &tokens;

    const uint32 tokens_size = tokens.Size();

    while (mTokenIndex < tokens_size) {
        mConfigEntries.Insert(ParseEntry());
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

    mConstants.Insert(ConfigEntry("TRUE", 1));
    mConstants.Insert(ConfigEntry("FALSE", 0));

    mConstants.Insert(ConfigEntry("OBJLAYER_WORLD", 0));
    mConstants.Insert(ConfigEntry("OBJLAYER_PLAYER", 1));

    mConstants.Insert(ConfigEntry("PHYS_STATIC", 0));
    mConstants.Insert(ConfigEntry("PHYS_DYNAMIC", 1));
}

} // namespace fx
