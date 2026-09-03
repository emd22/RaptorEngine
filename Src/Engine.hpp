#pragma once

namespace fx {

class PhysicsManager;
extern PhysicsManager* gPhysics;

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

class ThreadManager;
extern ThreadManager* gThreadManager;

class ScriptManager;
extern ScriptManager* gScriptManager;

class World;
extern World* gWorld;


namespace Globals {
void Init();
void Destroy();
} // namespace Globals

} // namespace fx
