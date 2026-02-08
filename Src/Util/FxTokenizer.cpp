#include "FxTokenizer.hpp"

FxTokenType FxTokenizer::GetTokenType(FxToken& token)
{
    if (token.Type == FxTokenType::DocComment) {
        return FxTokenType::DocComment;
    }

    // Check if the token is a number
    FxToken::IsNumericResult is_numeric = token.IsNumeric();

    switch (is_numeric) {
    case FxToken::IsNumericResult::Integer:
        return FxTokenType::Integer;
    case FxToken::IsNumericResult::Frfunctional:
        return FxTokenType::Float;
    case FxToken::IsNumericResult::NaN:
        break;
    }

    if (token.Length > 2) {
        // Checks if the token is a string
        if (token.Start[0] == '"' && token.Start[token.Length - 1] == '"') {
            return FxTokenType::String;
        }
    }

    if (token.Length == 1) {
        switch (token.Start[0]) {
        case '=':
            return FxTokenType::Equals;
        case '(':
            return FxTokenType::LParen;
        case ')':
            return FxTokenType::RParen;
        case '[':
            return FxTokenType::LBracket;
        case ']':
            return FxTokenType::RBracket;
        case '{':
            return FxTokenType::LBrace;
        case '}':
            return FxTokenType::RBrace;
        case '+':
            return FxTokenType::Plus;
        case '-':
            return FxTokenType::Minus;
        case '$':
            return FxTokenType::Dollar;
        case '.':
            return FxTokenType::Dot;
        case ',':
            return FxTokenType::Comma;
        case ';':
            return FxTokenType::Semicolon;
        }
    }

    return FxTokenType::Identifier;
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

        char* end_of_operator = mData;
        ++mData;

        SubmitTokenIfData(current_token, end_of_operator, mData);

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


void FxTokenizer::IncludeFile(char* path)
{
    FxFile file(path, FxFile::ModType::eRead, FxFile::DataType::eBinary);


    if (!file.IsFileOpen()) {
        FxLogError("Could not open include file '{}'", path);
        return;
    }

    uint32 include_size = 0;
    FxSlice<char> include_data = file.Read<char>();

    // Save the current state of the tokenizer
    SaveState();

    mData = include_data;
    mDataEnd = include_data + include_size;
    mInString = false;

    // Tokenize all of the included file
    Tokenize();

    // Restore back to previous state
    RestoreState();

    // FoxMemPool::Free(include_data);
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
        if (ch == '/' && ((mData + 1) < mDataEnd) && ((*(mData + 1)) == '/')) {
            SubmitTokenIfData(current_token);
            in_comment = true;

            ++mData;
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

        if (ch == '/' && ((mData + 1) < mDataEnd) && ((*(mData + 1)) == '*')) {
            SubmitTokenIfData(current_token);
            in_comment = true;

            ++mData;

            while ((mData + 1 < mDataEnd) && (ch = *(++mData))) {
                if (ch == '*' && ((mData + 1) < mDataEnd) && (*(mData + 1) == '/')) {
                    current_token.Start = mData;
                    break;
                }
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
                current_token.Type = FxTokenType::DocComment;
                SubmitTokenIfData(current_token);

                current_token.Type = FxTokenType::Unknown;

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
