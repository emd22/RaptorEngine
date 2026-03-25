#pragma once

class PhJolt;
class FxShaderCompiler;
class FxObjectManager;
class FxMaterialManager;
class FxMemPool;

extern PhJolt* gPhysics;
extern FxShaderCompiler* gShaderCompiler;
extern FxObjectManager* gObjectManager;
extern FxMaterialManager* gMaterialManager;
extern FxMemPool* gEnginePool;

namespace FxGlobals {
void Init();
void Destroy();
} // namespace FxGlobals
