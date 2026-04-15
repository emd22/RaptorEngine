#pragma once

#include <Asset/DataPack.hpp>
#include <Core/SizedArray.hpp>

namespace fx {


struct ShaderMacro
{
    const char* pcName;
    const char* pcValue;
};

class ShaderCompiler
{
public:
    enum class Result
    {
        eSuccess,
        eFailed,
    };

public:
    ShaderCompiler() = default;


    static void CompileAllShaders(const char* folder_path);

    static Result Compile(const char* path, DataPack& pack, const SizedArray<ShaderMacro>& macros,
                          bool do_db_flush = true);


    static bool CompileIfOutOfDate(const char* path, DataPack& pack, const SizedArray<ShaderMacro>& macros);

    static bool IsOutOfDate(const char* path);

    static void Destroy();
};

} // namespace fx
