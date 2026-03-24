#pragma once

class PhJolt;
class FxShaderCompiler;
class FxObjectManager;
class FxMemPool;

extern PhJolt* gPhysics;
extern FxShaderCompiler* gShaderCompiler;
extern FxObjectManager* gObjectManager;
extern FxMemPool* gEnginePool;

void FxEngineGlobalsInit();
void FxEngineGlobalsDestroy();
