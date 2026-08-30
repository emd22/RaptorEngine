#pragma once

namespace fx {

namespace physics {
class JoltPhysicsBackend;
}
extern physics::JoltPhysicsBackend* gPhysics;

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


namespace Globals {
void Init();
void Destroy();
} // namespace Globals

} // namespace fx
