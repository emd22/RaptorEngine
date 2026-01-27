#include "FxTokenizer.hpp"

using IsNumericResult = FxToken::IsNumericResult;

//////////////////////////////////////
// Token functions
//////////////////////////////////////

char* FxToken::GetHeapStr() const
{
    char* str = static_cast<char*>(std::malloc(Length + 1));

    if (str == nullptr) {
        FxPanic("FxTokenizer", "Error allocating heap string!");
        return nullptr;
    }

    strncpy(str, Start, Length);
    str[Length] = 0;

    return str;
}

FxHash32 FxToken::GetHash()
{
    if (Hash != 0) {
        return Hash;
    }
    return (Hash = FxHashStr32(Start, Length));
}

IsNumericResult FxToken::IsNumeric() const
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


int64 FxToken::ToInt() const
{
    char buffer[32];
    strncpy(buffer, Start, Length);
    buffer[Length] = 0;

    char* end = nullptr;
    return strtoll(buffer, &end, 10);
}

float32 FxToken::ToFloat() const
{
    char buffer[32];
    strncpy(buffer, Start, Length);
    buffer[Length] = 0;

    char* end = nullptr;
    return strtof(buffer, &end);
}

void FxToken::Clear()
{
    Start = nullptr;
    End = nullptr;
    Length = 0;
}

/////////////////////////////////////
// Tokenizer Functions
/////////////////////////////////////

FxTokenType FxTokenizer::GetTokenType(const FxToken& token) const
{
    if (token.Type == FxTokenType::eDocComment) {
        return FxTokenType::eDocComment;
    }

    // Check if the token is a number
    IsNumericResult is_numeric = token.IsNumeric();
    switch (is_numeric) {
    case IsNumericResult::Integer:
        return FxTokenType::eInteger;
    case IsNumericResult::Fractional:
        return FxTokenType::eFloat;
    case IsNumericResult::NaN:
        break;
    }

    if (token.Length > 2) {
        // Checks if the token is a string
        if (token.Start[0] == '"' && token.Start[token.Length - 1] == '"') {
            return FxTokenType::eString;
        }
    }

    if (token.Length == 1) {
        switch (token.Start[0]) {
        case '=':
            return FxTokenType::eEquals;
        case '(':
            return FxTokenType::eLParen;
        case ')':
            return FxTokenType::eRParen;
        case '[':
            return FxTokenType::eLBracket;
        case ']':
            return FxTokenType::eRBracket;
        case '{':
            return FxTokenType::eLBrace;
        case '}':
            return FxTokenType::eRBrace;
        case '+':
            return FxTokenType::ePlus;
        case '-':
            return FxTokenType::eMinus;
        case '$':
            return FxTokenType::eDollar;
        case '.':
            return FxTokenType::eDot;
        case ',':
            return FxTokenType::eComma;
        case ';':
            return FxTokenType::eSemicolon;
        }
    }

    return FxTokenType::eIdentifier;
}

void FxTokenizer::SubmitTokenIfData(FxToken& token, char* end_ptr, char* start_ptr)
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


bool FxTokenizer::CheckOperators(FxToken& current_token, char ch)
{
    if (ch == '.' && current_token.IsNumeric() != FxToken::IsNumericResult::NaN) {
        return false;
    }

    bool is_operator = (strchr(SingleCharOperators, ch) != NULL);

    if (is_operator) {
        // If there is data waiting, submit to the token list
        SubmitTokenIfData(current_token, mData);

        // Submit the operator as its own token
        current_token.Increment();

        SubmitTokenIfData(current_token, mData, (mData + 1));
        ++mData;

        return true;
    }

    return false;
}

uint32 FxTokenizer::ReadQuotedString(char* buffer, uint32 max_size, bool skip_on_success)
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

bool FxTokenizer::ExpectString(const char* expected_value, bool skip_on_success)
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


bool FxTokenizer::IsNewline(char ch)
{
    const bool is_newline = (ch == '\n');
    if (is_newline) {
        ++mFileLine;
        mStartOfLine = mData;
    }
    return is_newline;
}

void FxTokenizer::IncludeFile(char* path)
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

void FxTokenizer::TryReadInternalCall()
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


char* FxTokenizer::ReadFileData(FILE* fp, uint32* file_size_out)
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
        FxLogWarning("Error tokenizing buffer (BytesRead={}, Size={})", read_size, file_size);
    }

    return data;
}

void FxTokenizer::Tokenize()
{
    if (!mTokens.IsInited()) {
        mTokens.Create(512);
    }

    FxToken current_token;
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
                current_token.Type = FxTokenType::eDocComment;
                SubmitTokenIfData(current_token);

                current_token.Type = FxTokenType::eUnknown;

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
