#pragma once

#include <Core/FxHash.hpp>

class PhJolt;
class FxShaderCompiler;
class FxObjectManager;

extern PhJolt* gPhysics;
extern FxShaderCompiler* gShaderCompiler;
extern FxObjectManager* gObjectManager;

void FxEngineGlobalsInit();
void FxEngineGlobalsDestroy();
