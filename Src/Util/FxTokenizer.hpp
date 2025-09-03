#pragma once

#include <Core/FxHash.hpp>
#include <Core/FxMemory.hpp>
#include <Core/FxUtil.hpp>
#include <Core/Types.hpp>
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

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


    enum TokenType
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

    static const char* GetTypeName(TokenType type)
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

        /*if (type >= (sizeof(type_names) / sizeof(type_names[0]))) {
            return "Not Supported";
        }*/

        return type_names[type];
    }

    enum class IsNumericResult
    {
        NaN,
        Integer,
        Fractional
    };

    struct Token
    {
        char* Start = nullptr;
        char* End = nullptr;

        FxHash Hash = 0;
        TokenType Type = TokenType::Unknown;
        uint32 Length = 0;

        uint16 FileColumn = 0;
        uint32 FileLine = 0;

        void Print(bool no_newline = false) const
        {
            printf("Token: (T:%-10s) {%.*s} %c", GetTypeName(Type), Length, Start, (no_newline) ? ' ' : '\n');
        }

        void Increment() { ++Length; }

        inline std::string GetStr() const { return std::string(Start, Length); }

        inline char* GetHeapStr() const
        {
            char* str = static_cast<char*>(std::malloc(Length + 1));

            if (str == nullptr) {
                FxPanic("FxTokenizer", "Error allocating heap string!", 0);
                return nullptr;
            }

            strncpy(str, Start, Length);
            str[Length] = 0;

            return str;
        }

        FxHash GetHash()
        {
            if (Hash != 0) {
                return Hash;
            }
            return (Hash = FxHashStr(Start, Length));
        }

        IsNumericResult IsNumeric() const
        {
            char ch;

            IsNumericResult result = IsNumericResult::NaN;

            for (int i = 0; i < Length; i++) {
                ch = Start[i];

                // If there is a number preceding the dot then we are a fractional
                if (ch == '.' && result != IsNumericResult::NaN) {
                    result = IsNumericResult::Fractional;
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

        int64 ToInt() const
        {
            char buffer[32];
            strncpy(buffer, Start, Length);
            buffer[Length] = 0;

            char* end = nullptr;
            return strtoll(buffer, &end, 10);
        }

        float32 ToFloat() const
        {
            char buffer[32];
            strncpy(buffer, Start, Length);
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
    };

    FxTokenizer() = delete;

    FxTokenizer(char* data, uint32 buffer_size) : mData(data), mDataEnd(data + buffer_size), mStartOfLine(data) {}

    TokenType GetTokenType(Token& token)
    {
        if (token.Type == TokenType::DocComment) {
            return TokenType::DocComment;
        }

        // Check if the token is a number
        IsNumericResult is_numeric = token.IsNumeric();
        switch (is_numeric) {
        case IsNumericResult::Integer:
            return TokenType::Integer;
        case IsNumericResult::Fractional:
            return TokenType::Float;
        case IsNumericResult::NaN:
            break;
        }

        if (token.Length > 2) {
            // Checks if the token is a string
            if (token.Start[0] == '"' && token.Start[token.Length - 1] == '"') {
                // Remove start quote
                {
                    ++token.Start;
                    --token.Length;
                }
                // Remove end quote
                {
                    token.Length--;
                }
                return TokenType::String;
            }
        }

        if (token.Length == 1) {
            switch (token.Start[0]) {
            case '=':
                return TokenType::Equals;
            case '(':
                return TokenType::LParen;
            case ')':
                return TokenType::RParen;
            case '[':
                return TokenType::LBracket;
            case ']':
                return TokenType::RBracket;
            case '{':
                return TokenType::LBrace;
            case '}':
                return TokenType::RBrace;
            case '+':
                return TokenType::Plus;
            case '-':
                return TokenType::Minus;
            case '$':
                return TokenType::Dollar;
            case '.':
                return TokenType::Dot;
            case ',':
                return TokenType::Comma;
            case ';':
                return TokenType::Semicolon;
            }
        }

        return TokenType::Identifier;
    }

    void SubmitTokenIfData(Token& token, char* end_ptr = nullptr, char* start_ptr = nullptr)
    {
        if (token.IsEmpty()) {
            return;
        }

        if (end_ptr == nullptr) {
            end_ptr = mData;
        }

        if (start_ptr == nullptr) {
            start_ptr = mData;
        }

        token.End = mData;
        token.Type = GetTokenType(token);

        mTokens.Insert(token);
        token.Clear();

        token.Start = mData;

        uint16 column = 1;
        if (mStartOfLine) {
            column = static_cast<uint16>((mData - mStartOfLine));
        }
        token.FileColumn = column;
        token.FileLine = mFileLine + 1;
    }

    bool CheckOperators(Token& current_token, char ch)
    {
        if (ch == '.' && current_token.IsNumeric() != IsNumericResult::NaN) {
            return false;
        }

        bool is_operator = (strchr(SingleCharOperators, ch) != NULL);

        if (is_operator) {
            // If there is data waiting, submit to the token list
            SubmitTokenIfData(current_token, mData);

            // Submit the operator as its own token
            current_token.Increment();

            char* end_of_operator = mData;
            ++mData;

            SubmitTokenIfData(current_token, end_of_operator, mData);

            return true;
        }

        return false;
    }

    uint32 ReadQuotedString(char* buffer, uint32 max_size, bool skip_on_success = true)
    {
        char ch;
        char* data = mData;

        uint32 buf_size = 0;

        if (data >= mDataEnd) {
            return 0;
        }

        // Skip spaces and tabs
        while ((ch = *(data)) && (ch == ' ' || ch == '\t'))
            ++data;

        // Check if this is a string
        if (ch != '"') {
            puts("Not a string!");
            return 0;
        }

        // Eat the quote
        ++data;

        for (uint32 i = 0; i < max_size; i++) {
            ch = *data;

            if (ch == '"') {
                // Eat the quote
                ++data;
                break;
            }

            buffer[buf_size++] = ch;

            ++data;
        }

        if (skip_on_success) {
            mData = data;
        }

        buffer[buf_size] = 0;

        return buf_size;
    }

    bool ExpectString(const char* expected_value, bool skip_on_success = true)
    {
        char ch;
        int expected_index = 0;

        char* data = mData;

        while (data < mDataEnd && ((ch = *(data)))) {
            if (!expected_value[expected_index]) {
                break;
            }

            if (ch != expected_value[expected_index]) {
                return false;
            }

            ++expected_index;
            ++data;
        }

        if (skip_on_success) {
            mData = data;
        }

        return true;
    }

    void IncludeFile(char* path)
    {
        FILE* fp = FxUtil::FileOpen(path, "rb");
        if (!fp) {
            printf("Could not open include file '%s'\n", path);
            return;
        }

        uint32 include_size = 0;
        char* include_data = ReadFileData(fp, &include_size);

        // Save the current state of the tokenizer
        SaveState();

        mData = include_data;
        mDataEnd = include_data + include_size;
        mInString = false;

        // Tokenize all of the included file
        Tokenize();

        // Restore back to previous state
        RestoreState();

        // FxMemPool::Free(include_data);
    }

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

    char* ReadFileData(FILE* fp, uint32* file_size_out)
    {
        if (!fp || !file_size_out) {
            return nullptr;
        }

        std::fseek(fp, 0, SEEK_END);
        size_t file_size = std::ftell(fp);
        (*file_size_out) = file_size;

        std::rewind(fp);

        char* data = FxMemPool::Alloc<char>(file_size);

        size_t read_size = std::fread(data, 1, file_size, fp);
        if (read_size != file_size) {
            OldLog::Warning("Error tokenizing data(reading from file) (read=%zu, size=%zu)", read_size, file_size);
        }

        return data;
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

    void Tokenize()
    {
        if (!mTokens.IsInited()) {
            mTokens.Create(512);
        }

        Token current_token;
        current_token.Start = mData;

        bool in_comment = false;
        bool is_doccomment = false;

        char ch;

        while (mData < mDataEnd && ((ch = *(mData)))) {
            if (ch == '#') {
                SubmitTokenIfData(current_token);
                in_comment = true;

                //++mData;
                if (*(++mData) == '?') {
                    while ((ch = *(++mData))) {
                        if (isalnum(ch) || ch == '\n') {
                            current_token.Start = mData;
                            break;
                        }
                    }
                    is_doccomment = true;
                }
            }
            // If we are in a comment, skip until we hit a newline. Carriage return is eaten by our
            // below by the IsWhitespace check.
            if (in_comment) {
                if (!IsNewline(ch)) {
                    ++mData;
                    if (is_doccomment) {
                        current_token.Increment();
                    }
                    continue;
                }

                if (is_doccomment) {
                    current_token.Type = TokenType::DocComment;
                    SubmitTokenIfData(current_token);

                    current_token.Type = TokenType::Unknown;

                    is_doccomment = false;
                }

                // We hit a newline, mark as no longer in comment
                in_comment = false;
            }

            if (ch == '"') {
                // If we are not currently in a string, submit the token if there is data waiting
                if (!mInString) {
                    SubmitTokenIfData(current_token);
                }

                mInString = !mInString;
            }

            if (mInString) {
                ++mData;
                current_token.Increment();
                continue;
            }

            // Internal call
            if (ch == '@') {
                ++mData;
                TryReadInternalCall();

                current_token.Length = 0;
                current_token.Start = mData;

                continue;
            }

            if (IsWhitespace(ch)) {
                SubmitTokenIfData(current_token);

                mData++;
                current_token.Start = mData;
                continue;
            }

            if (CheckOperators(current_token, ch)) {
                continue;
            }

            mData++;
            current_token.Increment();
        }
        SubmitTokenIfData(current_token);
    }

    size_t GetTokenIndexInFile(Token& token) const
    {
        assert(token.Start > mData);
        return (token.Start - mData);
    }

    FxMPPagedArray<Token>& GetTokens() { return mTokens; }

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

    FxMPPagedArray<Token> mTokens;
};
