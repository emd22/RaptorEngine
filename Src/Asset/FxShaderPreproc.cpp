#include "FxShaderPreproc.hpp"

#include <cctype>
#include <cstdlib>

namespace FxShaderPreproc {

enum StringId
{
    // Program type definitions
    F_PROGRAM,
    FPT_VERTEX,
    FPT_PIXEL,

    // Reflection definitions
    F_REFLECT,
    FR_STRUCTBUFFER,

    // Test definitions
    F_PARAMTEST,
};

static constexpr const char* scStrings[] = {
    // Program type definitions
    "F_PROGRAM",
    "FPT_VERTEX",
    "FPT_PIXEL",

    // Reflection definitions
    "F_REFLECT",
    "FR_STRUCTBUFFER",

    // Test definitions
    "F_PARAMTEST",
};

constexpr const char* FSTR(StringId id) { return scStrings[static_cast<uint32>(id)]; }

struct State
{
public:
    State(const FxSlice<char> data) : FileData(data) {}

    char Get(uint32 offset) const
    {
        uint32 idx = Index + offset;
        if (idx > FileData.Size) {
            return 0;
        }

        return FileData.pData[idx];
    }

    char* GetCurrentPtr() { return FileData.pData + Index; }

    char Get() const { return Get(0); }

    void Next() { Index++; };
    void Skip(uint32 skip) { Index += skip; };

    void NextIfEqual(char p)
    {
        if (Get() == p) {
            Next();
        }
    }

    bool EatStringIfExists(const char* s)
    {
        uint32 offset = 0;
        char ch;

        while ((ch = *(s + offset)) != 0) {
            if (Get(offset) != ch) {
                return false;
            }

            ++offset;
        }

        Skip(offset);

        return true;
    }

public:
    FxSlice<char> FileData;
    uint32 Index = 0;
};

enum class ParseResult
{
    eError,
    eSuccess,
};

struct PPFuncEntry
{
    using FuncType = void(const std::vector<FxSlice<char>>& param, State& state, Result& result);

    PPFuncEntry() = delete;

    PPFuncEntry(const char* name, bool has_parameters, const std::function<FuncType> func)
        : pName(name), bHasParameters(has_parameters), Func(func)
    {
    }

    const char* pName;
    bool bHasParameters;
    const std::function<FuncType> Func;
};

#define REQUIRE_PARAMS(_params, _amt_req)                                                                              \
    if (_params.size() < _amt_req) {                                                                                   \
        FxLogError("SHADER: Not enough parameters found in preprocessor function!");                                   \
        return;                                                                                                        \
    }

static void ParseProgramDefinition(const std::vector<FxSlice<char>>& params, State& state, Result& result)
{
    REQUIRE_PARAMS(params, 1);

    const FxSlice<char>& program_type = params[0];

    // Set the new shader type
    if (!strncmp(program_type.pData, FSTR(FPT_VERTEX), program_type.Size)) {
        result.SetCurrentShader(RxShaderType::eVertex);
    }
    else if (!strncmp(program_type.pData, FSTR(FPT_PIXEL), program_type.Size)) {
        result.SetCurrentShader(RxShaderType::eFragment);
    }

    // Skip the ptr to be at our current position
    result.SetShaderBounds(FxSlice<char>(state.GetCurrentPtr(), 0));
}

static int32 ParamGetInt(const FxSlice<char>& param)
{
    char* endptr;

    printf("* GET INT FOR %.*s\n", param.Size, param.pData);

    // hacky...
    char restore = 0;
    // Save the end character, set to null terminator
    std::swap(restore, *(param.pData + param.Size));

    const int32 value = std::strtol(param.pData, &endptr, 10);

    // Restore end character
    std::swap(restore, *(param.pData + param.Size));

    return value;
}

static void ParseReflectionDefinition(const std::vector<FxSlice<char>>& params, State& state, Result& result)
{
    REQUIRE_PARAMS(params, 3);

    const FxSlice<char>& refl_type = params[0];

    const int32 set = ParamGetInt(params[1]);
    const int32 binding = ParamGetInt(params[2]);

    FxLogInfo("SET: {}, BINDING: {}", set, binding);
}


static void ParseParamTestDefinition(const std::vector<FxSlice<char>>& params, State& state, Result& result)
{
    printf("== PARAMTEST ==\n");

    for (const FxSlice<char>& param : params) {
        printf("PARAMETER: '%.*s'\n", param.Size, param.pData);
    }

    printf("=====\n");
}


static const PPFuncEntry PPFunctions[] = {
    PPFuncEntry(FSTR(F_PROGRAM), true, ParseProgramDefinition),
    PPFuncEntry(FSTR(F_REFLECT), true, ParseReflectionDefinition),
    PPFuncEntry(FSTR(F_PARAMTEST), true, ParseParamTestDefinition),
};


static void ParsePPFuncCall(State& state, Result& result)
{
    const PPFuncEntry* func = nullptr;

    for (uint32 index = 0; index < std::size(PPFunctions); index++) {
        func = &PPFunctions[index];

        if (state.EatStringIfExists(func->pName)) {
            break;
        }

        func = nullptr;
    }

    if (func && func->bHasParameters) {
        state.Next(); // LParen

        FxSlice<char> param(state.GetCurrentPtr(), 0);

        std::vector<FxSlice<char>> param_list;

        while (state.Get() != ')') {
            if (state.Get() == ',') {
                param_list.push_back(param);

                state.Next(); // Eat comma
                while (std::isspace(state.Get())) {
                    state.Next();
                }

                param.pData = state.GetCurrentPtr();
                param.Size = 0;

                continue;
            }

            ++param.Size;
            state.Next();
        }

        // Push the final parameter
        param_list.push_back(param);

        state.Next(); // RParen

        func->Func(param_list, state, result);
    }
}


Result Process(const FxSlice<char>& data)
{
    Result result {};

    State state(data);

    // Set the default(vertex) shader to be the size of the shader file. This is a fallback if there is no preprocessor
    // statements found.
    result.SetShaderBounds(FxSlice<char>(data.pData, 0));

    for (; state.Index < data.Size; state.Next()) {
        ParsePPFuncCall(state, result);

        ++result.GetShaderBounds().Size;
    }

    printf("= VERTEX PROGRAM =:\n%.*s\n\n", result.GetShaderBounds(RxShaderType::eVertex).Size,
           result.GetShaderBounds(RxShaderType::eVertex).pData);
    printf("= PIXEL  PROGRAM =:\n%.*s\n\n", result.GetShaderBounds(RxShaderType::eFragment).Size,
           result.GetShaderBounds(RxShaderType::eFragment).pData);

    return result;
}

}; // namespace FxShaderPreproc
