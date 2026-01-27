#pragma once

#include <Core/FxHash.hpp>
#include <Core/FxMemory.hpp>
#include <Core/FxTypes.hpp>
#include <Core/FxUtil.hpp>
#include <cassert>
#include <cstdlib>
#include <string>

enum class FxTokenType
{
    eUnknown,
    eIdentifier,

    eString,
    eInteger,
    eFloat,

    eEquals,

    eLParen,
    eRParen,

    eLBracket,
    eRBracket,

    eLBrace,
    eRBrace,

    ePlus,
    eDollar,
    eMinus,

    eQuestion,

    eDot,
    eComma,
    eSemicolon,

    eDocComment,
};

class FxTokenTypeUtil
{
public:
    static const char* GetName(FxTokenType type)
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
};

class FxToken
{
public:
    enum class IsNumericResult
    {
        NaN,
        Integer,
        Fractional
    };


public:
    void Print(bool no_newline = false) const
    {
        printf("Token: (T:%-10s) {%.*s} %c", FxTokenTypeUtil::GetName(Type), Length, Start, (no_newline) ? ' ' : '\n');
    }

    void Increment() { ++Length; }

    inline std::string GetStr() const { return std::string(Start, Length); }
    char* GetHeapStr() const;
    FxHash32 GetHash();

    IsNumericResult IsNumeric() const;

    int64 ToInt() const;
    float32 ToFloat() const;

    bool operator==(const char* str) const { return !strncmp(Start, str, Length); }
    bool IsEmpty() const { return (Start == nullptr || Length == 0); }

    void Clear();

public:
    char* Start = nullptr;
    char* End = nullptr;

    FxHash32 Hash = 0;
    FxTokenType Type = FxTokenType::eUnknown;
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

    FxTokenType GetTokenType(const FxToken& token) const;

    void SubmitTokenIfData(FxToken& token, char* end_ptr = nullptr, char* start_ptr = nullptr);

    void IncludeFile(char* path);

    bool CheckOperators(FxToken& current_token, char ch);
    void TryReadInternalCall();
    uint32 ReadQuotedString(char* buffer, uint32 max_size, bool skip_on_success = true);
    bool ExpectString(const char* expected_value, bool skip_on_success = true);
    bool IsNewline(char ch);

    char* ReadFileData(FILE* fp, uint32* file_size_out);
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
