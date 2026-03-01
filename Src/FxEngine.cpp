#include "FxEngine.hpp"

#include <Asset/FxShaderCompiler.hpp>
#include <FxObjectManager.hpp>
#include <Physics/PhJolt.hpp>

PhJolt* gPhysics = nullptr;
FxShaderCompiler* gShaderCompiler = nullptr;
FxObjectManager* gObjectManager = nullptr;


void FxEngineGlobalsInit()
{
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
