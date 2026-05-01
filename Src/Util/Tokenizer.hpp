#pragma once

#include <Core/PagedArray.hpp>
//
#include <Core/File.hpp>
#include <Core/Hash.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Engine.hpp>
#include <cassert>
#include <cstdlib>
#include <cstring>

namespace fx {

enum class eTokenType
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

    LessThan,
    GreaterThan,

    Plus,
    Dollar,
    Minus,

    Question,

    Dot,
    Comma,
    Semicolon,

    Equality,
    NotEqual,
    LessEqual,
    GreaterEqual,

    DocComment,
};


struct Token
{
    enum class eIsNumericResult
    {
        NaN,
        Integer,
        Fractional
    };

public:
    static const char* GetTypeName(eTokenType type);
    static bool IsTypeEqualityCheck(eTokenType type)
    {
        switch (type) {
        case eTokenType::LessThan:
        case eTokenType::GreaterThan:
        case eTokenType::LessEqual:
        case eTokenType::GreaterEqual:
        case eTokenType::Equality:
        case eTokenType::NotEqual:
            return true;
        default:;
        }

        return false;
    }


    void Print(bool no_newline = false) const
    {
        printf("Token[%d]: (T:%-10s) {%.*s} %c", Length, GetTypeName(Type), Length, Start, (no_newline) ? ' ' : '\n');
    }

    void Increment() { ++Length; }

    inline std::string GetStr() const { return std::string(Start, Length); }

    inline char* GetHeapStr() const
    {
        char* str = gEnginePool->Alloc<char>(Length + 1);

        if (str == nullptr) {
            Panic("FoxTokenizer", "Error allocating heap string!", 0);
            return nullptr;
        }

        std::strncpy(str, Start, Length);
        str[Length] = 0;

        return str;
    }

    Hash32 GetHash()
    {
        if (Hash != HashNull32) {
            return Hash;
        }
        return (Hash = HashData32(Slice<char>(Start, Length)));
    }

    eIsNumericResult IsNumeric() const;


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

    eTokenType Type = eTokenType::Unknown;
    uint32 Length = 0;

    uint16 FileColumn = 0;
    uint32 FileLine = 0;

private:
    Hash32 Hash = HashNull32;
};


class Tokenizer
{
private:
public:
    const char* SingleCharOperators = "=()[]{}<>+-$.,;?!";

    struct State
    {
        char* pDataStart = nullptr;
        char* pData = nullptr;
        char* pDataEnd = nullptr;
        bool bInString = false;

        uint32 FileLine = 0;
        char* pStartOfLine = nullptr;
    };


    Tokenizer() = delete;

    Tokenizer(char* data, uint32 buffer_size)
        : mpDataStart(data), mpData(data), mpDataEnd(data + buffer_size), mpLinePtr(data)
    {
        if (!TokenBuffer.IsInited()) {
            TokenBuffer.Create(512);
        }
    }

    void SubmitTokenIfData(Token& token, char* end_ptr = nullptr, char* start_ptr = nullptr);

    eTokenType GetTokenType(Token& token);
    bool CheckOperators(Token& current_token, char ch);
    uint32 ReadQuotedString(char* buffer, uint32 max_size, bool skip_on_success = true);
    bool ExpectString(const char* expected_value, bool skip_on_success = true);

    void TryReadInternalCall();
    void IncludeFile(const char* path);

    void Tokenize();

    void SetDataPtr(char* ptr)
    {
        mpDataStart = ptr;
        mpData = ptr;
    }

    size_t GetTokenIndexInFile(Token& token) const
    {
        assert(token.Start > mpData);
        return (token.Start - mpData);
    }

    PagedArray<Token>& GetTokens() { return TokenBuffer; }

    void SaveState()
    {
        mSavedState.pDataStart = mpData;
        mSavedState.pData = mpData;
        mSavedState.pDataEnd = mpDataEnd;
        mSavedState.bInString = mbInString;

        mSavedState.FileLine = mLineNumber;
        mSavedState.pStartOfLine = mpLinePtr;
    }

    void RestoreState()
    {
        mpDataStart = mSavedState.pDataStart;
        mpData = mSavedState.pData;
        mpDataEnd = mSavedState.pDataEnd;
        mbInString = mSavedState.bInString;

        mLineNumber = mSavedState.FileLine;
        mpLinePtr = mSavedState.pStartOfLine;
    }

    PagedArray<char*>& GetDataPtrs() { return DataPtrs; }


    ~Tokenizer();

private:
    bool IsWhitespace(char ch) { return (ch == ' ' || ch == '\t' || IsNewline(ch) || ch == '\r'); }

    bool IsNewline(char ch)
    {
        const bool is_newline = (ch == '\n');
        if (is_newline) {
            ++mLineNumber;
            mpLinePtr = mpData;
        }
        return is_newline;
    }


public:
    PagedArray<Token> TokenBuffer;
    PagedArray<char*> DataPtrs;

private:
    State mSavedState;

    char* mpDataStart = nullptr;
    char* mpData = nullptr;
    char* mpDataEnd = nullptr;

    bool mbInString = false;

    uint32 mLineNumber = 0;
    char* mpLinePtr = nullptr;
};

} // namespace fx

template <>
struct std::formatter<fx::Token>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::Token& obj, std::format_context& ctx) const
    {
        const std::string str(obj.Start, obj.Length);
        return std::format_to(ctx.out(), "(Type={}, {})", fx::Token::GetTypeName(obj.Type), str);
    }
};


template <>
struct std::formatter<fx::eTokenType>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(fx::eTokenType type, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "{}", fx::Token::GetTypeName(type));
    }
};
