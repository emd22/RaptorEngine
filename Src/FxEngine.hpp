#pragma once

#include <FxPhysicsJolt.hpp>
#include <Renderer/RxRenderBackend.hpp>

extern RxRenderBackend* gRenderer;
extern FxPhysicsJolt* gPhysics;

void FxEngineGlobalsInit();
void FxEngineGlobalsDestroy();
