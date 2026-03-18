#include "FxShaderPreproc.hpp"

namespace FxShaderPreproc {

enum StringId
{
    FPT_VERTEX,
    FPT_PIXEL,
};

static constexpr const char* scStrings[] = {
    "FPT_VERTEX",
    "FPT_PIXEL",
};

#define FSTR(_id) scStrings[_id]


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
    using FuncType = void(const FxSlice<char>& param, State& state, Result& result);

    PPFuncEntry() = delete;

    PPFuncEntry(const char* name, bool has_parameters, const std::function<FuncType> func)
        : pName(name), bParameters(has_parameters), Func(func)
    {
    }

    const char* pName;
    bool bParameters;
    const std::function<FuncType> Func;
};

static void ParseProgramDefinition(const FxSlice<char>& program_type, State& state, Result& result)
{
    printf("Name: %.*s\n", program_type.Size, program_type.pData);
    if (!strncmp(program_type.pData, FSTR(FPT_VERTEX), program_type.Size)) {
        result.SetCurrentShader(RxShaderType::eVertex);
        FxLogInfo("New shader: Vertex");
    }
    else if (!strncmp(program_type.pData, FSTR(FPT_PIXEL), program_type.Size)) {
        result.SetCurrentShader(RxShaderType::eFragment);
        FxLogInfo("New shader: Pixel");
    }
}


static const PPFuncEntry PPFunctions[] = {
    PPFuncEntry("F_PROGRAM", true, ParseProgramDefinition),
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

    if (func && func->bParameters) {
        state.Next(); // LParen

        FxSlice<char> param(state.GetCurrentPtr(), 0);

        while (state.Get() != ')') {
            ++param.Size;
            state.Next();
        }
        state.Next(); // RParen

        func->Func(param, state, result);
    }
}


// static void ParseProgramType(State& state, Result& result)
// {
//     state.NextIfEqual(' ');

//     constexpr uint32 cOpBufferSize = 80;

//     char op[cOpBufferSize];
//     uint32 op_index = 0;

//     while (state.Get() != '(') {
//         op[op_index++] = state.Get();
//         state.Next();
//     }

//     op[op_index] = 0;

//     if (strncmp(op, "PROGRAM", cOpBufferSize)) {
//         FxLogError("Unknown preproc keyword '{}' in shader", op);

//         return;
//     }

//     // Skip LParen
//     state.Next();

//     op_index = 0;

//     while (state.Get() != ')') {
//         op[op_index++] = state.Get();
//         state.Next();
//     }

//     op[op_index] = 0;


//     FxHash32 st_hash = FxHashStr32(op);

//     static constexpr FxHash32 scVertex = FxHashStr32("VERTEX");
//     static constexpr FxHash32 scPixel = FxHashStr32("PIXEL");

//     switch (st_hash) {
//     case scVertex:
//         result.SetCurrentShader(RxShaderType::eVertex);
//         break;
//     case scPixel:
//         result.SetCurrentShader(RxShaderType::eVertex);
//         break;
//     }

//     result.SetShaderBounds(FxSlice<char>(state.FileData.pData + state.Index, 0));

//     FxLogInfo("New shader type: {}", RxShaderUtil::TypeToName(result.CurrentType));

//     // Skip RParen
//     state.Next();
// }


Result Process(const FxSlice<char>& data)
{
    Result result {};

    State state(data);

    // Set the default(vertex) shader to be the size of the shader file. This is a fallback if there is no preprocessor
    // statements found.
    result.SetShaderBounds(FxSlice<char>(data.pData, 0));

    for (; state.Index < data.Size; state.Next()) {
        const uint32 chars_remaining = data.Size - state.Index;

        ParsePPFuncCall(state, result);
    }

    return result;
}

}; // namespace FxShaderPreproc
