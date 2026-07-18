#pragma once

namespace fx {

class PhJolt;
extern PhJolt* gPhysics;

class AssetManager;
extern AssetManager* gAssetManager;

class ShaderCompiler;
extern ShaderCompiler* gShaderCompiler;

class ObjectManager;
extern ObjectManager* gObjectManager;

class MaterialManager;
extern MaterialManager* gMaterialManager;

class MemPool;
extern MemPool* gEnginePool;
extern MemPool* gScriptMemPool;

class WorldGrid;
extern WorldGrid* gWorldGrid;

class TextureManager;
extern TextureManager* gTextureManager;


namespace Globals {
void Init();
void Destroy();
} // namespace Globals

} // namespace fx
