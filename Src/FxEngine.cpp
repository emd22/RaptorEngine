#include "FxEngine.hpp"

#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxRef.hpp>
#include <FxMaterial.hpp>
#include <FxObjectManager.hpp>
#include <Physics/PhJolt.hpp>
#include <Renderer/RxPipelineList.hpp>
#include <Renderer/RxRenderBackend.hpp>
#include <Renderer/RxRenderPassCache.hpp>

RxRenderBackend* gRenderer = nullptr;
PhJolt* gPhysics = nullptr;
FxShaderCompiler* gShaderCompiler = nullptr;
RxPipelineList* gPipelines = nullptr;
RxRenderPassCache* gRenderPassCache = nullptr;
FxObjectManager* gObjectManager = nullptr;


void FxEngineGlobalsInit()
{
    gRenderer = new RxRenderBackend;
    gPhysics = new PhJolt;
    gShaderCompiler = new FxShaderCompiler;
    // gRenderPassCache = new RxRenderPassCache;
    gPipelines = new RxPipelineList;
    gObjectManager = new FxObjectManager;
}

void FxEngineGlobalsDestroy()
{
    delete gRenderer;
    delete gPhysics;
    delete gShaderCompiler;
    delete gPipelines;
    delete gObjectManager;

    // delete gRenderPassCache;
}
