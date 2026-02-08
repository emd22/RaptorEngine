#pragma once

#include <Core/FxFile.hpp>
#include <Core/FxHash.hpp>
#include <Core/FxPagedArray.hpp>
#include <cassert>
#include <cstdlib>
#include <cstring>


enum class FxTokenType
{
    Unknown,
    Identifier,

    String,
    Integer,
    Float,

    Equals,

    LParen,
    RParen,

    LBracket,
    RBracket,

    LBrace,
    RBrace,

    Plus,
    Dollar,
    Minus,

    Question,

    Dot,
    Comma,
    Semicolon,

    DocComment,
};

struct FxToken
{
    enum class IsNumericResult
    {
        NaN,
        Integer,
        Frfunctional
    };

public:
    void Print(bool no_newline = false) const
    {
        printf("Token: (T:%-10s) {%.*s} %c", GetTypeName(Type), Length, Start, (no_newline) ? ' ' : '\n');
    }

    void Increment() { ++Length; }

    inline std::string GetStr() const { return std::string(Start, Length); }

    inline char* GetHeapStr() const
    {
        char* str = FxMemPool::Alloc<char>(Length + 1);

        if (str == nullptr) {
            FxPanic("FoxTokenizer", "Error allocating heap string!", 0);
            return nullptr;
        }

        std::strncpy(str, Start, Length);
        str[Length] = 0;

        return str;
    }

    FxHash64 GetHash()
    {
        if (Hash != 0) {
            return Hash;
        }
        return (Hash = FxHashStr64(Start, Length));
    }

    IsNumericResult IsNumeric() const
    {
        char ch;

        IsNumericResult result = IsNumericResult::NaN;

        for (int i = 0; i < Length; i++) {
            ch = Start[i];

            // If there is a number preceding the dot then we are a frfunctional
            if (ch == '.' && result != IsNumericResult::NaN) {
                result = IsNumericResult::Frfunctional;
                continue;
            }

            if ((ch >= '0' && ch <= '9')) {
                // If no numbers have been found yet then set to integer
                if (result == IsNumericResult::NaN) {
                    result = IsNumericResult::Integer;
                }
                continue;
            }

            // Not a number
            return IsNumericResult::NaN;
        }

        // Is numeric
        return result;
    }

    static const char* GetTypeName(FxTokenType type)
    {
        const char* type_names[] = {
            "Unknown",    "Identifier",

            "String",     "Integer",    "Float",

            "Equals",

            "LParen",     "RParen",

            "LBracket",   "RBracket",

            "LBrace",     "RBrace",

            "Plus",       "Dollar",     "Minus",

            "Question",

            "Dot",        "Comma",      "Semicolon",

            "DocComment",
        };

        return type_names[static_cast<uint32>(type)];
    }

    int64 ToInt() const
    {
        char buffer[32];

        std::strncpy(buffer, Start, Length);
        buffer[Length] = 0;

        char* end = nullptr;
        return strtoll(buffer, &end, 10);
    }

    float32 ToFloat() const
    {
        char buffer[32];
        std::strncpy(buffer, Start, Length);
        buffer[Length] = 0;

        char* end = nullptr;
        return strtof(buffer, &end);
    }

    bool operator==(const char* str) const { return !strncmp(Start, str, Length); }

    bool IsEmpty() const { return (Start == nullptr || Length == 0); }

    void Clear()
    {
        Start = nullptr;
        End = nullptr;
        Length = 0;
    }


public:
    char* Start = nullptr;
    char* End = nullptr;

    FxHash64 Hash = 0;
    FxTokenType Type = FxTokenType::Unknown;
    uint32 Length = 0;

    uint16 FileColumn = 0;
    uint32 FileLine = 0;
};

class FxTokenizer
{
private:
public:
    const char* SingleCharOperators = "=()[]{}+-$.,;?";

    struct State
    {
        char* Data = nullptr;
        char* DataEnd = nullptr;
        bool InString = false;

        uint32 FileLine = 0;
        char* StartOfLine = nullptr;
    };


    FxTokenizer() = delete;

    FxTokenizer(char* data, uint32 buffer_size) : mData(data), mDataEnd(data + buffer_size), mStartOfLine(data) {}

    FxTokenType GetTokenType(FxToken& token);

    void SubmitTokenIfData(FxToken& token, char* end_ptr = nullptr, char* start_ptr = nullptr);

    bool CheckOperators(FxToken& current_token, char ch);

    uint32 ReadQuotedString(char* buffer, uint32 max_size, bool skip_on_success = true);

    bool ExpectString(const char* expected_value, bool skip_on_success = true);

    void IncludeFile(char* path);

    void TryReadInternalCall()
    {
        if (ExpectString("include")) {
            char include_path[512];

            if (!ReadQuotedString(include_path, 512)) {
                puts("Error reading include path!");
                return;
            }

            IncludeFile(include_path);
        }
    }

    bool IsNewline(char ch)
    {
        const bool is_newline = (ch == '\n');
        if (is_newline) {
            ++mFileLine;
            mStartOfLine = mData;
        }
        return is_newline;
    }

    void Tokenize();

    size_t GetTokenIndexInFile(FxToken& token) const
    {
        assert(token.Start > mData);
        return (token.Start - mData);
    }

    FxPagedArray<FxToken>& GetTokens() { return mTokens; }

    void SaveState()
    {
        mSavedState.Data = mData;
        mSavedState.DataEnd = mDataEnd;
        mSavedState.InString = mInString;

        mSavedState.FileLine = mFileLine;
        mSavedState.StartOfLine = mStartOfLine;
    }

    void RestoreState()
    {
        mData = mSavedState.Data;
        mDataEnd = mSavedState.DataEnd;
        mInString = mSavedState.InString;

        mFileLine = mSavedState.FileLine;
        mStartOfLine = mSavedState.StartOfLine;
    }

private:
    bool IsWhitespace(char ch) { return (ch == ' ' || ch == '\t' || IsNewline(ch) || ch == '\r'); }

private:
    State mSavedState;

    char* mData = nullptr;
    char* mDataEnd = nullptr;

    bool mInString = false;

    uint32 mFileLine = 0;
    char* mStartOfLine = nullptr;

    FxPagedArray<FxToken> mTokens;
};


template <>
struct std::formatter<FxToken>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    constexpr auto format(const FxToken& obj, std::format_context& ctx) const
    {
        const std::string str(obj.Start, obj.Length);
        return std::format_to(ctx.out(), "(Type={}, {})", FxToken::GetTypeName(obj.Type), str);
    }
};
