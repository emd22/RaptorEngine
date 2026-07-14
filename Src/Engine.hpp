#pragma once

namespace fx {

class PhJolt;
class AssetManager;
class ShaderCompiler;
class ObjectManager;
class MaterialManager;
class MemPool;
class WorldGrid;

extern PhJolt* gPhysics;
extern AssetManager* gAssetManager;
extern ObjectManager* gObjectManager;
extern ShaderCompiler* gShaderCompiler;
extern MaterialManager* gMaterialManager;
extern MemPool* gEnginePool;
extern MemPool* gScriptMemPool;
extern WorldGrid* gWorldGrid;

namespace Globals {
void Init();
void Destroy();
} // namespace Globals

} // namespace fx
