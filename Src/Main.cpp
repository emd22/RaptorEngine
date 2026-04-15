
#define VMA_DEBUG_LOG(...) LogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/ConfigFile.hpp>
#include <Asset/DataPack.hpp>
#include <Asset/ShaderCompiler.hpp>
#include <Core/Defer.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/String.hpp>
#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/RxGlobals.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    fx::gEnginePool = new fx::MemPool;
    fx::gEnginePool->Create(FX_MEMORY_ENGINE_POOL_SIZE);

    fx::Globals::Init();

    {
        fx::FoxtrotGame game {};
    }

    fx::Globals::Destroy();
    fx::RxGlobals::Destroy();

    Defer(
        []()
        {
            delete fx::gEnginePool;
            fx::gEnginePool = nullptr;
        });

    return 0;
}
