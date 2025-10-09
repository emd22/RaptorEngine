#include "FxEngine.hpp"


RxRenderBackend* gRenderer = nullptr;
FxPhysicsJolt* gPhysics = nullptr;
FxShaderCompiler* gShaderCompiler = nullptr;

void FxEngineGlobalsInit()
{
    gRenderer = new RxRenderBackend;
    gPhysics = new FxPhysicsJolt;
    gShaderCompiler = new FxShaderCompiler;
}

void FxEngineGlobalsDestroy()
{
    delete gRenderer;
    delete gPhysics;
    delete gShaderCompiler;
}
