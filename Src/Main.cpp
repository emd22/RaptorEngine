
#define VMA_DEBUG_LOG(...) LogWarning(LC_MEMORY, __VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/ConfigFile.hpp>
#include <Asset/DataPack.hpp>
#include <Asset/Font/Font.hpp>
#include <Asset/ShaderCompiler.hpp>
#include <Asset/ShaderPreproc.hpp>
#include <Core/Defer.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/String.hpp>
#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/Globals.hpp>
#include <Script/FoxScript.hpp>
#include <thread>

FX_SET_MODULE_NAME("Main")


static void N_ScriptLog(fx::script::FoxVM* vm, const fx::SizedArray<fx::script::FoxValue>& args)
{
    for (fx::script::FoxValue& arg : args) {
        switch (arg.Type) {
        case fx::eFoxType::INT:
            fx::LogInfo(fx::LC_SCRIPT, "{}", arg.Get<fx::int32>());
            break;
        case fx::eFoxType::FLOAT:
            fx::LogInfo(fx::LC_SCRIPT, "{}", arg.Get<fx::float32>());
            break;
        case fx::eFoxType::STRING:
            fx::LogInfo(fx::LC_SCRIPT, "{}", arg.Get<const char*>());
            break;
        default:;
        }
    }
}

int main()
{
    fx::gEnginePool = new fx::MemPool;
    fx::gEnginePool->Create(FX_MEMORY_ENGINE_POOL_SIZE);

    fx::gScriptMemPool = new fx::MemPool;
    fx::gScriptMemPool->Create(1024 * 64);

    {
        fx::script::FoxScript script(FX_BASE_DIR "/Scripts/Test.fox");

        script.RegisterProc(fx::HashStr32("LOG"), fx::eFoxProcFlags::None, { fx::eFoxType::STRING }, N_ScriptLog);
        fx::script::FoxValue value = script.CallProc(script.GetSymbol("Init"), {});
        value.Print();

        // while (true) {
        //     value = script.Update();
        //     if (!script.IsPaused()) {
        //         value.Print();
        //         break;
        //     }
        //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // }
    }


    // fx::renderer::Globals::Init();

    // {
    //     fx::FoxtrotGame game {};
    // }

    // fx::Globals::Destroy();
    // fx::renderer::Globals::Destroy();

    // Defer(
    //     []()
    //     {
    //         delete fx::gEnginePool;
    //         fx::gEnginePool = nullptr;
    //     });


    return 0;
}
