#include "Tokenizer.hpp"

#include <Core/MemPool/MemPool.hpp>
#include <Core/Path.hpp>
#include <Engine.hpp>

namespace fx {

const char* Token::GetTypeName(eTokenType type)
{
    const char* type_names[] = {
        "Unknown",    "Identifier",

        "String",     "Integer",     "Float",

        "Equals",

        "LParen",     "RParen",

        "LBracket",   "RBracket",

        "LBrace",     "RBrace",

        "LessThan",   "GreaterThan",

        "Plus",       "Dollar",      "Minus",     "Asterisk",

        "Question",

        "Dot",        "Comma",       "Semicolon",

        "Equality",   "NotEqual",    "LessEqual", "GreaterEqual",

        "DocComment",
    };

    return type_names[static_cast<uint32>(type)];
}


Token::eIsNumericResult Token::IsNumeric() const
{
    char ch;

    Token::eIsNumericResult result = Token::eIsNumericResult::NaN;

    for (int i = 0; i < Length; i++) {
        ch = Start[i];

        // If there is a number preceding the dot then we are a frfunctional
        if (ch == '.' && result != Token::eIsNumericResult::NaN) {
            result = Token::eIsNumericResult::Fractional;
            continue;
        }

        if ((ch >= '0' && ch <= '9')) {
            // If no numbers have been found yet then set to integer
            if (result == Token::eIsNumericResult::NaN) {
                result = Token::eIsNumericResult::Integer;
            }
            continue;
        }

        if (ch == 'f' && (i == Length - 1) && result != Token::eIsNumericResult::NaN) {
            result = Token::eIsNumericResult::Fractional;
            continue;
        }

        // Not a number
        return Token::eIsNumericResult::NaN;
    }

    // Is numeric
    return result;
}


/////////////////////////////////////
// Tokenizer
/////////////////////////////////////

eTokenType Tokenizer::GetTokenType(Token& token)
{
    if (token.Type == eTokenType::DocComment) {
        return eTokenType::DocComment;
    }

    // Check if the token is a number
    Token::eIsNumericResult is_numeric = token.IsNumeric();

    switch (is_numeric) {
    case Token::eIsNumericResult::Integer:
        return eTokenType::Integer;
    case Token::eIsNumericResult::Fractional:
        return eTokenType::Float;
    case Token::eIsNumericResult::NaN:
        break;
    }

    if (token.Type == eTokenType::String) {
        return eTokenType::String;
    }

    if (token.Length == 1) {
        switch (token.Start[0]) {
        case '=':
            return eTokenType::Equals;
        case '(':
            return eTokenType::LParen;
        case ')':
            return eTokenType::RParen;
        case '[':
            return eTokenType::LBracket;
        case ']':
            return eTokenType::RBracket;
        case '{':
            return eTokenType::LBrace;
        case '}':
            return eTokenType::RBrace;
        case '<':
            return eTokenType::LessThan;
        case '>':
            return eTokenType::GreaterThan;
        case '+':
            return eTokenType::Plus;
        case '-':
            return eTokenType::Minus;
        case '$':
            return eTokenType::Dollar;
        case '*':
            return eTokenType::Asterisk;
        case '.':
            return eTokenType::Dot;
        case ',':
            return eTokenType::Comma;
        case ';':
            return eTokenType::Semicolon;
        }
    }

    else if (token.Length == 2) {
        // Find equality tokens (e.g !=, >=, ==)
        if (token.Start[1] == '=') {
            switch (token.Start[0]) {
            case '=':
                return eTokenType::Equality;
            case '!':
                return eTokenType::NotEqual;
            case '<':
                return eTokenType::LessEqual;
            case '>':
                return eTokenType::GreaterEqual;
            }
        }
    }

    return eTokenType::Identifier;
}


void Tokenizer::SubmitTokenIfData(Token& token, char* end_ptr, char* start_ptr)
{
    if (token.IsEmpty()) {
        return;
    }

    if (end_ptr == nullptr) {
        end_ptr = mpData;
    }

    if (start_ptr == nullptr) {
        start_ptr = mpData;
    }

    token.End = mpData;
    token.Type = GetTokenType(token);

    TokenBuffer.Insert(token);
    token.Clear();

    token.Start = mpData;

    uint16 column = 1;
    if (mpLinePtr) {
        column = static_cast<uint16>((mpData - mpLinePtr));
    }

    token.FileColumn = column;
    token.FileLine = mLineNumber + 1;
}


bool Tokenizer::CheckOperators(Token& current_token, char ch)
{
    if (ch == '.' && current_token.IsNumeric() != Token::eIsNumericResult::NaN) {
        return false;
    }

    bool is_operator = (strchr(SingleCharOperators, ch) != NULL);

    if (is_operator) {
        const char* double_operators = "=!<>";

        bool is_double_operator = (strchr(double_operators, ch) != NULL);

        if ((is_double_operator && mpData[1] == '=')) {
            // If there is data waiting, submit to the token list
            SubmitTokenIfData(current_token, mpData);

            current_token.Increment();
            current_token.Increment();

            char* end_of_operator = mpData;
            ++mpData;
            ++mpData;

            SubmitTokenIfData(current_token, end_of_operator, mpData);
            return true;
        }
    }

    if (is_operator) {
        // If there is data waiting, submit to the token list
        SubmitTokenIfData(current_token, mpData);

        // Submit the operator as its own token
        current_token.Increment();

        char* end_of_operator = mpData;
        ++mpData;

        SubmitTokenIfData(current_token, end_of_operator, mpData);

        return true;
    }

    return false;
}


uint32 Tokenizer::ReadQuotedString(char* buffer, uint32 max_size, bool skip_on_success)
{
    char ch;
    char* data = mpData;

    uint32 buf_size = 0;

    if (data >= mpDataEnd) {
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
        mpData = data;
    }

    buffer[buf_size] = 0;

    return buf_size;
}


bool Tokenizer::ExpectString(const char* expected_value, bool skip_on_success)
{
    char ch;
    int expected_index = 0;

    char* data = mpData;

    while (data < mpDataEnd && ((ch = *(data)))) {
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
        mpData = data;
    }

    return true;
}


void Tokenizer::IncludeFile(const char* path)
{
    Path vpath(path);

    // If there is no extension on the file, add .fox
    if (!vpath.HasExtension()) {
        String* basename = vpath.BaseName();
        (*basename) += ".fox";
    }

    File file(path, File::eModType::Read, File::eDataType::Binary);


    if (!file.IsFileOpen()) {
        LogError(LC_SCRIPT, "Could not open include file '{}'", path);
        return;
    }


    // Save the current state of the tokenizer
    SaveState();

    Slice<char> include_data = file.Read<char>();

    DataPtrs.Insert(include_data.pData);

    SetDataPtr(include_data.pData);
    mpDataEnd = include_data.pData + include_data.Size;

    // Tokenize all of the included file
    Tokenize();

    // gEnginePool->Free<char>(mpData);

    // Restore back to previous state
    RestoreState();
}


void Tokenizer::Tokenize()
{
    Token current_token;
    current_token.Start = mpData;

    bool in_comment = false;

    char ch;

    while (mpData < mpDataEnd && ((ch = *(mpData)))) {
        if (ch == '\r') {
            ++mpData;
            continue;
        }

        if (ch == '/' && ((mpData + 1) < mpDataEnd) && ((*(mpData + 1)) == '/')) {
            SubmitTokenIfData(current_token);
            in_comment = true;

            ++mpData;
        }

        if (ch == '/' && ((mpData + 1) < mpDataEnd) && ((*(mpData + 1)) == '*')) {
            SubmitTokenIfData(current_token);
            in_comment = true;

            ++mpData;

            while ((mpData + 1 < mpDataEnd) && (ch = *(++mpData))) {
                if (ch == '*' && ((mpData + 1) < mpDataEnd) && (*(mpData + 1) == '/')) {
                    current_token.Start = mpData;
                    break;
                }
            }
        }

        // If we are in a comment, skip until we hit a newline. Carriage return is eaten by our
        // below by the IsWhitespace check.
        if (in_comment) {
            if (!IsNewline(ch)) {
                ++mpData;
                continue;
            }

            // We hit a newline, mark as no longer in comment
            in_comment = false;
        }

        // Read in a string
        if (ch == '"') {
            // If we are not currently in a string, submit the token if there is data waiting
            SubmitTokenIfData(current_token);

            current_token.Type = eTokenType::String;
            ++current_token.Start;

            // Skip quote
            ++mpData;

            while (mpData < mpDataEnd && (*mpData) != '"') {
                current_token.Increment();
                ++mpData;
            }

            if (mpData < mpDataEnd) {
                ++mpData;
            }

            continue;
        }


        // Internal call
        if (ch == '#') {
            ++mpData;
            TryReadInternalCall();

            current_token.Length = 0;
            current_token.Start = mpData;

            continue;
        }

        if (IsWhitespace(ch)) {
            SubmitTokenIfData(current_token);

            mpData++;
            current_token.Start = mpData;
            continue;
        }

        if (CheckOperators(current_token, ch)) {
            continue;
        }

        mpData++;
        current_token.Increment();
    }

    SubmitTokenIfData(current_token);
}


void Tokenizer::TryReadInternalCall()
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

Tokenizer::~Tokenizer() { TokenBuffer.Destroy(); }

} // namespace fx
