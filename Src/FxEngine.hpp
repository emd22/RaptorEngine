#pragma once

#include <Core/FxHash.hpp>

class RxRenderBackend;
class FxPhysicsJolt;
class FxShaderCompiler;
class RxPipelineList;
class RxRenderPassCache;

extern RxRenderBackend* gRenderer;
extern FxPhysicsJolt* gPhysics;
extern FxShaderCompiler* gShaderCompiler;
extern RxPipelineList* gPipelines;
// extern RxRenderPassCache* gRenderPassCache;

inline constexpr const char* gPipelineGeometryName = "Geometry";
inline constexpr FxHash gPipelineGeometry = FxHashStr(gPipelineGeometryName);

inline constexpr const char* gPipelineLightingName = "Lighting";
inline constexpr FxHash gPipelineLighting = FxHashStr(gPipelineLightingName);

inline constexpr const char* gPipelineCompositionName = "Composition";
inline constexpr FxHash gPipelineComposition = FxHashStr(gPipelineCompositionName);

void FxEngineGlobalsInit();
void FxEngineGlobalsDestroy();
