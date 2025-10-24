#include "FxEngine.hpp"


RxRenderBackend* gRenderer = nullptr;
FxPhysicsJolt* gPhysics = nullptr;
FxShaderCompiler* gShaderCompiler = nullptr;
RxPipelineList* gPipelines = nullptr;

void FxEngineGlobalsInit()
{
    gRenderer = new RxRenderBackend;
    gPhysics = new FxPhysicsJolt;
    gShaderCompiler = new FxShaderCompiler;
    gPipelines = new RxPipelineList;
}

void FxEngineGlobalsDestroy()
{
    delete gRenderer;
    delete gPhysics;
    delete gShaderCompiler;
    delete gPipelines;
}
