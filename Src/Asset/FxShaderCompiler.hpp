#pragma once

#include <Renderer/Backend/RxShader.hpp>

class FxShaderCompiler
{
public:
    FxShaderCompiler() = default;

    static void CompileAllShaders(const char* folder_path);

    static void Compile(const char* path, const char* output_path, bool do_db_flush = true);
    static void Destroy();
};
