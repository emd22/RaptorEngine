
#define VMA_DEBUG_LOG(...) LogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/ConfigFile.hpp>
#include <Asset/DataPack.hpp>
#include <Asset/Font/Font.hpp>
#include <Asset/ShaderCompiler.hpp>
#include <Asset/ShaderPreproc.hpp>
#include <Core/Defer.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/String.hpp>
#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/Globals.hpp>
#include <Script/FoxScript.hpp>

FX_SET_MODULE_NAME("Main")


int main()
{
    fx::gEnginePool = new fx::MemPool;
    fx::gEnginePool->Create(FX_MEMORY_ENGINE_POOL_SIZE);

    fx::gScriptMemPool = new fx::MemPool;
    fx::gScriptMemPool->Create(1024 * 1024 * 2);


    fx::File fp = fx::File(FX_BASE_DIR "/PreprocTest.txt", fx::File::eModType::Read, fx::File::eDataType::Binary);

    fx::Slice<char> data = fp.Read<char>();

    fx::SizedArray<fx::ShaderMacro> macros = { fx::ShaderMacro { .pcName = "C", .pcValue = "1" } };

    fx::ShaderPreproc::Result preproc = fx::ShaderPreproc::Process(data, macros);
    fx::ShaderPreproc::DebugSaveToDisk("./PreprocResult", preproc);


    // fx::renderer::Globals::Init();

    // {
    //     fx::FoxtrotGame game {};
    // }

    // fx::renderer::Globals::Destroy();
    // fx::Globals::Destroy();

    // Defer(
    //     []()
    //     {
    //         delete fx::gEnginePool;
    //         fx::gEnginePool = nullptr;
    //     });

    return 0;
}
