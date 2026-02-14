#include "FxConfigFile.hpp"

#include <Core/FxFile.hpp>
#include <Core/FxHash.hpp>
#include <Util/FxTokenizer.hpp>
#include <string>

/////////////////////////////////////
// Config entry functions
/////////////////////////////////////


FxConfigEntry::FxConfigEntry(FxConfigEntry&& other)
{
    Type = other.Type;
    other.Type = ValueType::eNone;

    if (Type == ValueType::eString) {
        mStringValue = other.mStringValue;
        other.mStringValue = nullptr;
    }
    else if (Type == ValueType::eInt) {
        mIntValue = other.mIntValue;
    }
    else if (Type == ValueType::eFloat) {
        mFloatValue = other.mFloatValue;
    }

    Name = other.Name;
    NameHash = other.NameHash;
    Members = std::move(other.Members);

    other.NameHash = 0;
    other.Name.clear();
}

void FxConfigEntry::AddMember(FxConfigEntry&& entry)
{
    if (!Members.IsInited()) {
        Members.Create();
    }

    Members.Insert(std::move(entry));
}

std::string FxConfigEntry::AsString() const
{
    std::string member_list = "";

    if (Type == ValueType::eStruct) {
        for (const FxConfigEntry& entry : Members) {
            member_list += std::format("{} = {},\n", entry.Name, entry.AsString());
        }

        return std::format("{{\n{}}} ({} members)", member_list, Members.Size());
    }

    switch (Type) {
    case ValueType::eNone:
        return "";
    case ValueType::eInt:
        return std::to_string(mIntValue);
    case ValueType::eFloat:
        return std::to_string(mFloatValue);
    case ValueType::eString:
        return std::string(mStringValue);
    case ValueType::eStruct:
        break;
    }

    return "";
}


FxConfigEntry::~FxConfigEntry()
{
    if (Type == ValueType::eString && mStringValue != nullptr) {
        free(mStringValue);
    }

    Type = ValueType::eNone;

    mStringValue = nullptr;
    Members.Destroy();
}


/////////////////////////////////////
// Config file functions
/////////////////////////////////////

void FxConfigFile::Load(const std::string& path)
{
    FxFile file(path.c_str(), FxFile::eRead, FxFile::eBinary);

    if (!file.IsFileOpen()) {
        return;
    }

    FxSlice<char> file_buffer = file.Read<char>();

    FxTokenizer tokenizer(file_buffer.pData, file_buffer.Size);
    tokenizer.Tokenize();

    Parse(tokenizer.GetTokens());
}

static bool TokenIsValue(const FxToken& token, const char* str) { return (!strncmp(token.Start, str, token.Length)); }

static FxConfigEntry::ValueType GetValueTokenType(const FxToken& token)
{
    using VType = FxConfigEntry::ValueType;

    VType current_type = VType::eNone;

    FxToken::IsNumericResult numeric_result = token.IsNumeric();
    if (numeric_result != FxToken::IsNumericResult::eNaN) {
        if (numeric_result == FxToken::IsNumericResult::eInteger) {
            return VType::eInt;
        }
        else {
            return VType::eFloat;
        }
    }

    for (uint32 i = 0; i < token.Length; i++) {
        char ch = token.Start[i];

        if (ch == '"') {
            current_type = VType::eString;
        }
    }

    return current_type;
}

bool FxConfigFile::EatToken(FxTokenType type)
{
    const bool correct_token = (GetToken()->Type == type);

    if (!correct_token) {
        FxLogError("Config: Expected '{}' but found '{}'", FxToken::GetTypeName(type),
                   FxToken::GetTypeName(GetToken()->Type));
        return false;
    }

    NextToken();

    return true;
}

FxConfigEntry FxConfigFile::ParseEntry()
{
    // [IDENTIFIER] = [INT | FLOAT | STRING | STRUCT]

    FxConfigEntry entry;

    FxToken* token = GetToken();

    entry.Name = (token->GetStr());
    entry.NameHash = FxHashStr64(token->GetStr().c_str());

    EatToken(FxTokenType::eIdentifier);
    EatToken(FxTokenType::eEquals);

    // Parse struct
    // [IDENTIFIER] = { [ENTRY]... }
    if (GetToken()->Type == FxTokenType::eLBrace) {
        EatToken(FxTokenType::eLBrace);

        entry.Type = FxConfigEntry::ValueType::eStruct;

        // Add each entry as a member of the current entry
        while (GetToken()->Type != FxTokenType::eRBrace) {
            FxConfigEntry member = ParseEntry();
            FxLogInfo("Adding entry {} -> {}", member.Name, member.AsString());
            entry.AddMember(std::move(member));
        }

        EatToken(FxTokenType::eRBrace);

        return entry;
    }

    // Parse single value entry
    // [IDENTIFIER] = [INT | FLOAT | STRING]

    FxToken* value_token = GetToken();

    using VType = FxConfigEntry::ValueType;

    entry.Type = GetValueTokenType(*value_token);

    switch (entry.Type) {
    case VType::eNone:
        break;
    case VType::eString:
        // Remove the first quote
        value_token->Start++;
        value_token->Length--;

        // Remove the ending quote
        value_token->Length--;
        entry.Set(value_token->GetStr());

        break;
    case VType::eInt:
        entry.Set<int64>(value_token->ToInt());
        break;
    case VType::eFloat:
        entry.Set<float32>(value_token->ToFloat());
        break;
    default:
        break;
    }

    NextToken();

    return entry;
}

void FxConfigFile::Parse(FxPagedArray<FxToken>& tokens)
{
    mConfigEntries.Create(32);

    mpTokens = &tokens;

    const uint32 tokens_size = tokens.Size();

    while (mTokenIndex < tokens_size) {
        mConfigEntries.Insert(ParseEntry());
    }

    mpTokens = nullptr;
}

const FxConfigEntry* FxConfigFile::GetEntry(FxHash64 requested_name_hash) const
{
    for (const FxConfigEntry& entry : mConfigEntries) {
        FxLogWarning("Checking: {} :: {}", entry.NameHash, requested_name_hash);
        if (entry.NameHash == requested_name_hash) {
            return &entry;
        }
    }

    return nullptr;
}


void FxConfigFile::Write(const std::string& path)
{
    FxFile file(path.c_str(), FxFile::eWrite, FxFile::eText);

    if (!file.IsFileOpen()) {
        return;
    }

    for (const FxConfigEntry& entry : mConfigEntries) {
        file.WriteMulti(entry.Name.c_str(), " = ", entry.AsString(), '\n');
    }

    file.Close();
}
