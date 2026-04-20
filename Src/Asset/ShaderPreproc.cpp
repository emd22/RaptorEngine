#include "ShaderPreproc.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <functional>

namespace fx {

namespace ShaderPreproc {

static constexpr uint32 scDataPageSize = 512;

enum eStringId
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

constexpr const char* FStr(eStringId id) { return scStrings[static_cast<uint32>(id)]; }
constexpr Hash32 FHash(eStringId id) { return HashStr32(FStr(id)); }

struct State
{
public:
    State(const Slice<char> data) : FileData(data) {}

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

    void NextChar() { Index++; };
    void Skip(uint32 skip) { Index += skip; };

    void NextIfEqual(char p)
    {
        if (Get() == p) {
            NextChar();
        }
    }

    bool TryReadString(const char* s)
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
    Slice<char> FileData;
    uint32 Index = 0;

    uint32 CurrentLine = 0;
};

enum class eParseResult
{
    Error,
    Success,
};

struct PPFuncEntry
{
    using FuncType = void(const std::vector<Slice<char>>& param, State& state, Result& result);

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
        LogError("SHADER: Not enough parameters found in preprocessor function!");                                     \
        return;                                                                                                        \
    }

static int32 ParamGetInt(const Slice<char>& param)
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

static void ParseProgramDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
    REQUIRE_PARAMS(params, 1);

    Hash32 type_hash = HashData32(params[0]);

    switch (type_hash) {
    case FHash(FPT_VERTEX):
        result.SetCurrentShader(renderer::eShaderType::Vertex);
        break;
    case FHash(FPT_PIXEL):
        result.SetCurrentShader(renderer::eShaderType::Pixel);
        break;
    case FHash(FPT_ALL):
        result.bBroadcastToAllPrograms = true;
        return;
    default:;
    }

    // Notify DXC about the actual line number to make the HLSL error messages less ass
    result.InsertString(std::format("#line {}\n", state.CurrentLine + 1));
}


static void ParseReflectionDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
    REQUIRE_PARAMS(params, 3);

    const Slice<char>& refl_type = params[0];

    const int32 set = ParamGetInt(params[1]);
    const int32 binding = ParamGetInt(params[2]);

    Hash32 refl_hash = HashData32(refl_type);
    eEntryType type = eEntryType::StructuredBuffer;

    switch (refl_hash) {
    case FHash(FR_STRUCTBUFFER):
        type = eEntryType::StructuredBuffer;
        break;
    case FHash(FR_UNIFORMBUFFER):
        type = eEntryType::UniformBuffer;
        break;
    case FHash(FR_SAMPLER2D):
        type = eEntryType::Sampler2D;
        break;
    default:;
    }

    result.Reflection.emplace_back(type, set, binding);
}


static void ParseParamTestDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
    printf("== PARAMTEST ==\n");

    for (const Slice<char>& param : params) {
        printf("PARAMETER: '%.*s'\n", param.Size, param.pData);
    }

    printf("=====\n");
}


static const PPFuncEntry PPFunctions[] = {
    PPFuncEntry(FStr(F_PROGRAM), true, ParseProgramDefinition),
    PPFuncEntry(FStr(F_REFLECT), true, ParseReflectionDefinition),
    PPFuncEntry(FStr(F_PARAMTEST), true, ParseParamTestDefinition),
};

static void WriteCurrentCharToProgram(State& state, Result& result)
{
    char ch = state.Get();

    if (ch == '\n') {
        ++state.CurrentLine;
    }

    if (result.bBroadcastToAllPrograms) {
        result.GetBuffer(renderer::eShaderType::Vertex).Insert(ch);
        result.GetBuffer(renderer::eShaderType::Pixel).Insert(ch);

        return;
    }

    result.GetBuffer().Insert(ch);
}

static bool ParseIfdef(State& state, Result& result, const SizedArray<ShaderMacro>& macros)
{
    char read_macro[256];
    uint32 read_index = 0;


    auto WriteUntilHash = [&]()
    {
        while (state.Get() != '#') {
            // Continue saving each character until the condition is closed
            WriteCurrentCharToProgram(state, result);
            state.NextChar();
        }
    };

    auto SkipUntilHash = [&]()
    {
        while (state.Get() != '#') {
            // Continue saving each character until the condition is closed
            state.NextChar();
        }
    };

    if (state.TryReadString("#ifdef ")) {
        read_index = 0;

        while (state.Get() != '\n') {
            char ch = state.Get();

            if (ch == '\r') {
                state.NextChar();
                continue;
            }

            read_macro[read_index++] = ch;
            state.NextChar();
        }

        state.NextIfEqual('\r');
        state.NextIfEqual('\n');

        read_macro[read_index] = 0;

        bool macro_found = false;

        printf("MACRO SIZE: %d\n", read_index);
        printf("MACRO: '%.*s'\n", read_index, read_macro);

        for (const ShaderMacro& macro : macros) {
            if (!std::strncmp(macro.pcName, read_macro, read_index)) {
                macro_found = true;
                break;
            }
        }

        if (macro_found) {
            WriteUntilHash();

            if (state.TryReadString("#else")) {
                SkipUntilHash();
            }
        }
        else {
            // Skip the body of the ifdef
            SkipUntilHash();

            // Write the else condition
            if (state.TryReadString("#else")) {
                WriteUntilHash();
            }
        }

        // Eat the final endif
        state.TryReadString("#endif");
        state.NextIfEqual('\r');
        state.NextIfEqual('\n');

        return true;
    }

    return false;
}

static void ParsePPFuncCall(State& state, Result& result)
{
    const PPFuncEntry* func = nullptr;

    for (uint32 index = 0; index < std::size(PPFunctions); index++) {
        func = &PPFunctions[index];

        if (state.TryReadString(func->pName)) {
            break;
        }

        func = nullptr;
    }

    if (func && func->bHasParameters) {
        state.NextChar(); // Skip LParen

        Slice<char> param(state.GetCurrentPtr(), 0);

        std::vector<Slice<char>> param_list;

        // Parse arguments
        while (state.Get() != ')') {
            if (state.Get() == ',') {
                param_list.push_back(param);

                state.NextChar(); // Skip comma
                while (std::isspace(state.Get())) {
                    state.NextChar();
                }

                param.pData = state.GetCurrentPtr();
                param.Size = 0;

                continue;
            }

            ++param.Size;
            state.NextChar();
        }


        // Push the final parameter
        param_list.push_back(param);
        state.NextChar(); // Skip RParen

        func->Func(param_list, state, result);
    }
}


Result Process(const Slice<char>& data, const SizedArray<ShaderMacro>& macros)
{
    Result result {};
    result.GetBuffer().SetPageSize(scDataPageSize);

    State state(data);

    while (state.Index < data.Size) {
        // If there is a comment, skip until the end
        if (state.Get() == '/' && state.Get(1) == '/') {
            char ch;
            while ((ch = state.Get()) && ch != '\n') {
                state.NextChar();
            }
        }

        ParsePPFuncCall(state, result);

        if (state.Get() == '#' && ParseIfdef(state, result, macros)) {
            continue;
        }

        WriteCurrentCharToProgram(state, result);
        state.NextChar();
    }

    return result;
}


static void SaveProgramToDisk(const char* name, renderer::eShaderType shader_type, const Result& result)
{
    const DataBuffer& buffer = result.ProgramData[static_cast<uint32>(shader_type)];

    if (buffer.Size > 0) {
        std::string sname = std::string(name) + "_" + renderer::ShaderUtil::TypeToName(shader_type) + ".hlsl";
        File file(sname.c_str(), File::eModType::Write, File::eDataType::Binary);
        file.Write(Slice<char>(buffer.pData, buffer.Size));
        file.Close();
    }
}

void DebugSaveToDisk(const char* name, const Result& result)
{
    SaveProgramToDisk(name, renderer::eShaderType::Vertex, result);
    SaveProgramToDisk(name, renderer::eShaderType::Pixel, result);
}

}; // namespace ShaderPreproc

} // namespace fx
