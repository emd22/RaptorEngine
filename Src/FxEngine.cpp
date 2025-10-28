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
    // gRenderPassCache = new RxRenderPassCache;
    gPipelines = new RxPipelineList;
}

void FxEngineGlobalsDestroy()
{
    delete gRenderer;
    delete gPhysics;
    delete gShaderCompiler;
    delete gPipelines;
    // delete gRenderPassCache;
}
