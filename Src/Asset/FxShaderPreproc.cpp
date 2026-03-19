#include "FxShaderPreproc.hpp"

#include <cctype>
#include <cstdlib>

namespace FxShaderPreproc {

static constexpr uint32 scDataPageSize = 512;

enum StringId
{
    // Program type definitions
    F_PROGRAM,
    FPT_VERTEX,
    FPT_PIXEL,
    FPT_ALL,

    // Reflection definitions
    F_REFLECT,
    FR_STRUCTBUFFER,
    FR_UNIFORMBUFFER,
    FR_SAMPLER2D,

    // Test definitions
    F_PARAMTEST,
};

static constexpr const char* scStrings[] = {
    // Program type definitions
    "F_PROGRAM",
    "FPT_VERTEX",
    "FPT_PIXEL",
    "FPT_ALL",

    // Reflection definitions
    "F_REFLECT",
    "FR_STRUCTBUFFER",
    "FR_UNIFORMBUFFER",
    "FR_SAMPLER2D",

    // Test definitions
    "F_PARAMTEST",
};

constexpr const char* FStr(StringId id) { return scStrings[static_cast<uint32>(id)]; }
constexpr FxHash32 FHash(StringId id) { return FxHashStr32(FStr(id)); }

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

    uint32 CurrentLine = 0;
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

static int32 ParamGetInt(const FxSlice<char>& param)
{
    char* endptr;

    // hacky...
    char restore = 0;
    std::swap(restore, *(param.pData + param.Size));
    const int32 value = std::strtol(param.pData, &endptr, 10);
    std::swap(restore, *(param.pData + param.Size));

    return value;
}

/////////////////////////////////////
// Preprocessor Definitions
/////////////////////////////////////

static void ParseProgramDefinition(const std::vector<FxSlice<char>>& params, State& state, Result& result)
{
    REQUIRE_PARAMS(params, 1);

    FxHash32 type_hash = FxHashData32(params[0]);

    switch (type_hash) {
    case FHash(FPT_VERTEX):
        result.SetCurrentShader(RxShaderType::eVertex);
        break;
    case FHash(FPT_PIXEL):
        result.SetCurrentShader(RxShaderType::eFragment);
        break;
    case FHash(FPT_ALL):
        result.bBroadcastToAllPrograms = true;
        return;
    default:;
    }

    // Notify DXC about the actual line number to make the HLSL error messages less ass
    result.InsertString(std::format("#line {}\n", state.CurrentLine + 1));
}


static void ParseReflectionDefinition(const std::vector<FxSlice<char>>& params, State& state, Result& result)
{
    REQUIRE_PARAMS(params, 3);

    const FxSlice<char>& refl_type = params[0];

    const int32 set = ParamGetInt(params[1]);
    const int32 binding = ParamGetInt(params[2]);

    FxHash32 refl_hash = FxHashData32(refl_type);
    EntryType type = EntryType::eStructuredBuffer;

    switch (refl_hash) {
    case FHash(FR_STRUCTBUFFER):
        type = EntryType::eStructuredBuffer;
        break;
    case FHash(FR_UNIFORMBUFFER):
        type = EntryType::eUniformBuffer;
        break;
    case FHash(FR_SAMPLER2D):
        type = EntryType::eSampler2D;
        break;
    default:;
    }

    result.Reflection.emplace_back(type, set, binding);
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
    PPFuncEntry(FStr(F_PROGRAM), true, ParseProgramDefinition),
    PPFuncEntry(FStr(F_REFLECT), true, ParseReflectionDefinition),
    PPFuncEntry(FStr(F_PARAMTEST), true, ParseParamTestDefinition),
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
        state.Next(); // Skip LParen

        FxSlice<char> param(state.GetCurrentPtr(), 0);

        std::vector<FxSlice<char>> param_list;

        // Parse arguments
        while (state.Get() != ')') {
            if (state.Get() == ',') {
                param_list.push_back(param);

                state.Next(); // Skip comma
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
        state.Next(); // Skip RParen

        func->Func(param_list, state, result);
    }
}


Result Process(const FxSlice<char>& data)
{
    Result result {};

    State state(data);

    result.GetBuffer().SetPageSize(scDataPageSize);

    // Set the default(vertex) shader to be the size of the shader file. This is a fallback if there is no preprocessor
    // statements found.
    // result.SetShaderBounds(FxSlice<char>(data.pData, 0));

    for (; state.Index < data.Size; state.Next()) {
        ParsePPFuncCall(state, result);

        char ch = state.Get();

        if (result.bBroadcastToAllPrograms) {
            result.GetBuffer(RxShaderType::eVertex).Insert(ch);
            result.GetBuffer(RxShaderType::eFragment).Insert(ch);

            continue;
        }

        if (ch == '\n') {
            ++state.CurrentLine;
        }

        result.GetBuffer().Insert(ch);
    }

    return result;
}


static void SaveProgramToDisk(const char* name, RxShaderType shader_type, const Result& result)
{
    const DataBuffer& buffer = result.ProgramData[static_cast<uint32>(shader_type)];

    if (buffer.Size > 0) {
        std::string sname = std::string(name) + "_" + RxShaderUtil::TypeToName(shader_type) + ".hlsl";
        FxFile file(sname.c_str(), FxFile::ModType::eWrite, FxFile::DataType::eBinary);
        file.Write(FxSlice<char>(buffer.pData, buffer.Size));
        file.Close();
    }
}

void DebugSaveToDisk(const char* name, const Result& result)
{
    SaveProgramToDisk(name, RxShaderType::eVertex, result);
    SaveProgramToDisk(name, RxShaderType::eFragment, result);
}

}; // namespace FxShaderPreproc
