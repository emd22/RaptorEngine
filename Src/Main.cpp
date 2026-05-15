
#define VMA_DEBUG_LOG(...) LogWarning(LC_MEMORY, __VA_ARGS__)

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


static void N_PrintX(fx::script::FoxVM* vm, const fx::SizedArray<fx::script::FoxValue>& args)
{
    fx::LogInfo(fx::LC_SCRIPT, "Printed Value: {}", args[0].Get<fx::int32>());
}

int main()
{
    fx::gEnginePool = new fx::MemPool;
    fx::gEnginePool->Create(FX_MEMORY_ENGINE_POOL_SIZE);

    fx::gScriptMemPool = new fx::MemPool;
    fx::gScriptMemPool->Create(1024 * 1024 * 2);

    { // fx::script::FoxScript script;

        // script.Load(FX_BASE_DIR "/Scripts/Test.fox");
        // script.RegisterProc(fx::HashStr32("PrintX"), false, 1, &N_PrintX);

        // fx::script::FoxSymbol* sym = script.GetSymbol(fx::HashStr32("Init"));

        // if (!sym) {
        //     fx::LogError("Cannot find symbol!");
        // }
        // fx::script::FoxValue result = script.CallProc(sym, {});

        // fx::LogInfo("result: {}", result);
    }


    fx::renderer::Globals::Init();

    {
        fx::FoxtrotGame game {};
    }

    fx::LogInfo("===== MEMPOOL STATS =====");
    fx::LogInfo("Bytes Used: {}", fx::gEnginePool->GetBytesUsed());
    fx::LogInfo("Pool Size:  {}", fx::gEnginePool->GetCapacity());
    fx::LogInfo("=========================");

    fx::Globals::Destroy();
    fx::renderer::Globals::Destroy();

    Defer(
        []()
        {
            delete fx::gEnginePool;
            fx::gEnginePool = nullptr;
        });

    return 0;
}
