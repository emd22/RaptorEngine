#include "FxEngine.hpp"

#include <Asset/FxShaderCompiler.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <FxObjectManager.hpp>
#include <Physics/PhJolt.hpp>


PhJolt* gPhysics = nullptr;
FxShaderCompiler* gShaderCompiler = nullptr;
FxObjectManager* gObjectManager = nullptr;
FxMemPool* gEnginePool = nullptr;

void FxEngineGlobalsInit()
{
    gEnginePool = new FxMemPool;
    gEnginePool->Create(FX_MEMORY_ENGINE_POOL_SIZE);

    gPhysics = new PhJolt;
    gShaderCompiler = new FxShaderCompiler;
    gObjectManager = new FxObjectManager;
}

void FxEngineGlobalsDestroy()
{
    delete gPhysics;
    gPhysics = nullptr;

    delete gShaderCompiler;
    gShaderCompiler = nullptr;

    delete gObjectManager;
    gObjectManager = nullptr;
}
