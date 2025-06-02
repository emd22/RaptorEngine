#include "FxConfig.hpp"

#include <Core/Log.hpp>
#include <Core/FxHash.hpp>

enum TokenType
{
    Unknown,
    Identifier,
    Equals,
    Number,
    String,
};

void FxConfig::Load(const char *filename)
{
    mFile = std::fopen(filename, "r");

    if (!mFile) {
        Log::Error("Error loading config file '%s'", filename);
        return;
    }

    ParseConfig();
}

static std::string GetToken(char **line, char delim)
{
    char token[512];
    int token_length = 0;

    char ch;

    char *ptr = *line;

    while ((ch = *(ptr++)) && (ch != delim)) {
        token[token_length++] = ch;
    }
    token[token_length] = 0;

    (*line) = ptr;

    return std::string(token, token_length);
}

static TokenType EatToken(char **line, TokenType expected)
{

}

void FxConfig::ParseConfig()
{
    if (!mFile) {
        return;
    }

    char current_line[512];

    while (fgets(current_line, sizeof(current_line), mFile)) {
        char *ptr = current_line;
        std::string entry_name = GetToken(&ptr, ' ');

        if ((*ptr) != '=') {

        }


        Log::Info("Entry name: %s", entry_name.c_str());
    }

}
