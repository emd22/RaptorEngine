#pragma once

#include <Renderer/Backend/RxShader.hpp>

class FxShaderCompiler
{
public:
    FxShaderCompiler() = default;

    static void Compile(const char* path, const char* output_path);
    static void Destroy();
};
