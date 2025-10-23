#pragma once

#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxRef.hpp>
#include <Physics/FxPhysicsJolt.hpp>
#include <Renderer/RxRenderBackend.hpp>

extern RxRenderBackend* gRenderer;
extern FxPhysicsJolt* gPhysics;
extern FxShaderCompiler* gShaderCompiler;

inline constexpr const char* gPipelineGeometryName = "Geometry";
inline constexpr FxHash gPipelineGeometry = FxHashStr(gPipelineGeometryName);

inline constexpr const char* gPipelineLightingName = "Lighting";
inline constexpr FxHash gPipelineLighting = FxHashStr(gPipelineLightingName);

inline constexpr const char* gPipelineCompositionName = "Composition";
inline constexpr FxHash gPipelineComposition = FxHashStr(gPipelineCompositionName);

void FxEngineGlobalsInit();
void FxEngineGlobalsDestroy();
