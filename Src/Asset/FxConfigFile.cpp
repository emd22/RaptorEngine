#include "FxConfigFile.hpp"

#include <Core/FxFile.hpp>
#include <Core/FxHash.hpp>
#include <Util/FxTokenizer.hpp>
#include <string>

void FxConfigFile::Load(const std::string& path)
{
    FxFile file(path.c_str(), FxFile::eRead, FxFile::eBinary);

    if (!file.IsFileOpen()) {
        return;
    }

    FxSlice<char> file_buffer = file.Read<char>();

    FxTokenizer tokenizer(file_buffer.pData, file_buffer.Size);
    tokenizer.Tokenize();

    ParseEntries(tokenizer.GetTokens());
}

static bool TokenIsValue(const FxToken& token, const char* str) { return (!strncmp(token.Start, str, token.Length)); }

static FxConfigEntry::ValueType GetValueTokenType(const FxToken& token)
{
    using VType = FxConfigEntry::ValueType;

    VType current_type = VType::eNone;

    FxToken::IsNumericResult numeric_result = token.IsNumeric();
    if (numeric_result != FxToken::IsNumericResult::NaN) {
        if (numeric_result == FxToken::IsNumericResult::Integer) {
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

void FxConfigFile::ParseEntries(FxPagedArray<FxToken>& tokens)
{
    mConfigEntries.Create(32);

    FxConfigEntry entry;

    const uint32 tokens_size = tokens.Size();

    for (uint32 i = 0; i < tokens_size; i++) {
        const FxToken& token = tokens[i];

        entry.Name = (token.GetStr());
        entry.NameHash = FxHashStr64(token.GetStr().c_str());


        // Eat the equals token
        if ((++i) >= tokens_size) {
            break;
        }

        // Token is not equals, print warning and continue
        if (!TokenIsValue(tokens[i], "=")) {
            FxLogWarning("Expected '=' but found '{:.{}}'", tokens[i].Start, tokens[i].Length);
            continue;
        }

        // Get the value token
        FxToken& value_token = tokens[++i];

        using VType = FxConfigEntry::ValueType;

        entry.Type = GetValueTokenType(value_token);

        switch (entry.Type) {
        case VType::eNone:
            break;
        case VType::eString:
            // Remove the first quote
            value_token.Start++;
            value_token.Length--;

            // Remove the ending quote
            value_token.Length--;
            entry.Set(value_token.GetStr());
            break;
        case VType::eInt:
            entry.Set<int64>(value_token.ToInt());
            break;
        case VType::eFloat:
            entry.Set<float32>(value_token.ToFloat());
            break;
        default:
            break;
        }

        mConfigEntries.Insert(std::move(entry));
    }
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
