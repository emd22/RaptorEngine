#pragma once

#include <Core/FxSizedArray.hpp>

struct FxShaderMacro
{
    const char* pcName;
    const char* pcValue;
};

class FxShaderCompiler
{
public:
    FxShaderCompiler() = default;

    static void CompileAllShaders(const char* folder_path);

    static void Compile(const char* path, const char* output_path, const FxSizedArray<FxShaderMacro>& macros,
                        bool do_db_flush = true);

    static void Destroy();
};
