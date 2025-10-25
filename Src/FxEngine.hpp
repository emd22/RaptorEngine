#pragma once

#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxRef.hpp>
#include <Physics/FxPhysicsJolt.hpp>
#include <Renderer/RxPipelineList.hpp>
#include <Renderer/RxRenderBackend.hpp>
#include <Renderer/RxRenderPassCache.hpp>

extern RxRenderBackend* gRenderer;
extern FxPhysicsJolt* gPhysics;
extern FxShaderCompiler* gShaderCompiler;
extern RxPipelineList* gPipelines;
extern RxRenderPassCache* gRenderPassCache;

inline constexpr const char* gPipelineGeometryName = "Geometry";
inline constexpr FxHash gPipelineGeometry = FxHashStr(gPipelineGeometryName);

inline constexpr const char* gPipelineLightingName = "Lighting";
inline constexpr FxHash gPipelineLighting = FxHashStr(gPipelineLightingName);

inline constexpr const char* gPipelineCompositionName = "Composition";
inline constexpr FxHash gPipelineComposition = FxHashStr(gPipelineCompositionName);

void FxEngineGlobalsInit();
void FxEngineGlobalsDestroy();
