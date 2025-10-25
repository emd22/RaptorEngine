#include "FxEngine.hpp"


RxRenderBackend* gRenderer = nullptr;
FxPhysicsJolt* gPhysics = nullptr;
FxShaderCompiler* gShaderCompiler = nullptr;
RxPipelineList* gPipelines = nullptr;
RxRenderPassCache* gRenderPassCache = nullptr;

void FxEngineGlobalsInit()
{
    gRenderer = new RxRenderBackend;
    gPhysics = new FxPhysicsJolt;
    gShaderCompiler = new FxShaderCompiler;
    gPipelines = new RxPipelineList;
    gRenderPassCache = new RxRenderPassCache;
}

void FxEngineGlobalsDestroy()
{
    delete gRenderer;
    delete gRenderPassCache;
    delete gPhysics;
    delete gShaderCompiler;
    delete gPipelines;
}
