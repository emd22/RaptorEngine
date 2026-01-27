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

static inline bool IsNumber(char ch) { return (ch >= '0' && ch <= '9'); }

static FxConfigEntry::ValueType GetValueTokenType(const FxToken& token)
{
    using VType = FxConfigEntry::ValueType;

    VType current_type = VType::None;

    for (uint32 i = 0; i < token.Length; i++) {
        char ch = token.Start[i];

        if (IsNumber(ch) && (current_type == VType::None || current_type == VType::Int)) {
            current_type = VType::Int;
            continue;
        }

        if (ch == '"') {
            current_type = VType::Str;
        }

        if (current_type == VType::None) {
            current_type = VType::Identifier;
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

        entry.NameHash = FxHashStr32(token.Start, token.Length);
        entry.Name = token.GetStr();

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
        case VType::None:
            break;
        case VType::Str:
            // Remove the first quote
            value_token.Start++;
            value_token.Length--;

            // Remove the ending quote
            value_token.Length--;
            [[fallthrough]];
        case VType::Identifier:
            entry.ValueStr = value_token.GetHeapStr();
            break;
        case VType::Int:
            entry.ValueInt = value_token.ToInt();
            break;
        default:
            break;
        }

        mConfigEntries.Insert(entry);
    }
}

const FxConfigEntry* FxConfigFile::GetEntry(uint32 requested_name_hash) const
{
    for (const FxConfigEntry& entry : mConfigEntries) {
        if (entry.NameHash == requested_name_hash) {
            return &entry;
        }
    }

    return nullptr;
}
