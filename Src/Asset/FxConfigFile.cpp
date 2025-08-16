#include <string>
#include "FxConfigFile.hpp"

#include <stdio.h>

#include <Core/FxHash.hpp>
#include <Util/FxTokenizer.hpp>

static FILE* FileOpen(const char* path, const char* mode)
{
#ifdef _WIN32
    FILE* fp = nullptr;
    errno_t result = fopen_s(&fp, path, mode);

    if (result != 0) {
        return nullptr;
    }

    return fp;
#else
    return fopen(path, mode);
#endif
}

void FxConfigFile::Load(const std::string& path)
{
    FILE* fp = FileOpen(path.c_str(), "rb");
    if (fp == nullptr) {
        Log::Error("Could not open config file at '%s'", path.c_str());
        return;
    }

    std::fseek(fp, 0, SEEK_END);
    size_t file_size = std::ftell(fp);
    std::rewind(fp);

    char* file_buffer = FxMemPool::Alloc<char>(file_size);

    size_t read_size = std::fread(file_buffer, 1, file_size, fp);
    if (read_size != file_size) {
        Log::Warning("Error reading all data from config file at '%s' (read=%zu, size=%zu)", path.c_str(), read_size, file_size);
    }

    FxTokenizer tokenizer(file_buffer, read_size);
    tokenizer.Tokenize();

    ParseEntries(tokenizer.GetTokens());

    fclose(fp);
}

static bool TokenIsValue(const FxTokenizer::Token& token, const char* str)
{
    return (!strncmp(token.Start, str, token.Length));
}

static inline bool IsNumber(char ch)
{
    return (ch >= '0' && ch <= '9');
}

static FxConfigEntry::ValueType GetValueTokenType(const FxTokenizer::Token& token)
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

void FxConfigFile::ParseEntries(FxMPPagedArray<FxTokenizer::Token>& tokens)
{
    FxConfigEntry entry;

    const uint32 tokens_size = tokens.Size();

    for (uint32 i = 0; i < tokens_size; i++) {
        const FxTokenizer::Token& token = tokens[i];

        entry.NameHash = FxHashStr(token.Start, token.Length);
        entry.Name = token.GetStr();

        // Eat the equals token
        if ((++i) >= tokens_size) {
            break;
        }
        // Token is not equals, print warning and continue
        if (!TokenIsValue(tokens[i], "=")) {
            Log::Warning("Expected '=' but found '%.*s'", tokens[i].Length, tokens[i].Start);
            continue;
        }

        // Get the value token
        FxTokenizer::Token& value_token = tokens[++i];

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

        mConfigEntries.emplace_back(entry);
    }
}
