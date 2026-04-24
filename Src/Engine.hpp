#pragma once

namespace fx {

class PhJolt;
class AxManager;
class ShaderCompiler;
class ObjectManager;
class MaterialManager;
class MemPool;

extern PhJolt* gPhysics;
extern AxManager* gAssetManager;
extern ObjectManager* gObjectManager;
extern ShaderCompiler* gShaderCompiler;
extern MaterialManager* gMaterialManager;
extern MemPool* gEnginePool;
extern MemPool* gScriptMemPool;

namespace Globals {
void Init();
void Destroy();
} // namespace Globals

} // namespace fx
