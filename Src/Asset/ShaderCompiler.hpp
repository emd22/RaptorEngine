#pragma once

#include <Asset/DataPack.hpp>
#include <Core/SizedArray.hpp>

namespace fx {

struct ProgramData;

struct ShaderMacro
{
    const char* pcName;
    const char* pcValue;
};

class ShaderCompiler
{
public:
    enum class eResult
    {
        Success,
        Failed,
    };


public:
    ShaderCompiler() = default;

    static void CompileAllShaders(const char* folder_path);

    static eResult Compile(const char* path, DataPack& pack, const SizedArray<ShaderMacro>& macros,
                           bool do_db_flush = true);

    static ProgramData GetProgramData(const Hash64 program_id, DataPack& pack);

    static bool CompileIfOutOfDate(const char* path, DataPack& pack, const SizedArray<ShaderMacro>& macros);
    static bool IsOutOfDate(const char* path);

    static void Destroy();
};

} // namespace fx
