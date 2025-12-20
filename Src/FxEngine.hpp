#pragma once

#include <Core/FxHash.hpp>

class RxRenderBackend;
class PhJolt;
class FxShaderCompiler;
class RxPipelineList;
class RxRenderPassCache;
class FxObjectManager;

extern RxRenderBackend* gRenderer;
extern PhJolt* gPhysics;
extern FxShaderCompiler* gShaderCompiler;
extern RxPipelineList* gPipelines;

extern FxObjectManager* gObjectManager;

// extern RxRenderPassCache* gRenderPassCache;

inline constexpr const char* gPipelineGeometryName = "Geometry";
inline constexpr FxHash32 gPipelineGeometry = FxHashStr32(gPipelineGeometryName);

inline constexpr const char* gPipelineLightingName = "Lighting";
inline constexpr FxHash32 gPipelineLighting = FxHashStr32(gPipelineLightingName);

inline constexpr const char* gPipelineCompositionName = "Composition";
inline constexpr FxHash32 gPipelineComposition = FxHashStr32(gPipelineCompositionName);

void FxEngineGlobalsInit();
void FxEngineGlobalsDestroy();
