#include "Engine.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ShaderCompiler.hpp>
#include <CVar.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/Thread/ThreadManager.hpp>
#include <InGameEditor.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Script/ScriptManager.hpp>
#include <Texture/TextureManager.hpp>
#include <World.hpp>
#include <WorldGrid.hpp>

namespace fx {

PhysicsManager* gPhysics = nullptr;
ShaderCompiler* gShaderCompiler = nullptr;

// Managers
AssetManager* gAssetManager = nullptr;
ObjectManager* gObjectManager = nullptr;
TextureManager* gTextureManager = nullptr;
MaterialManager* gMaterialManager = nullptr;
ThreadManager* gThreadManager = nullptr;

MemPool* gEnginePool = nullptr;
MemPool* gScriptMemPool = nullptr;

WorldGrid* gWorldGrid = nullptr;
World* gWorld = nullptr;
ProbeManager* gProbeManager = nullptr;
ScriptManager* gScriptManager = nullptr;

EditorMode* gSelectedEditorMode = nullptr;
CVarManager* gCVars = nullptr;


#define DESTROY_GLOBAL(name_)                                                                                          \
	delete name_;                                                                                                      \
	name_ = nullptr


namespace Globals {

void Init()
{
	gPhysics = new PhysicsManager;
	gAssetManager = new AssetManager;
	gShaderCompiler = new ShaderCompiler;
	gObjectManager = new ObjectManager;
	gMaterialManager = new MaterialManager;
	gWorldGrid = new WorldGrid;
	gTextureManager = new TextureManager;
	gThreadManager = new ThreadManager;
	gScriptManager = new ScriptManager;
	gWorld = new World;
	gProbeManager = new ProbeManager;
	gCVars = new CVarManager;
}


void Destroy()
{
	DESTROY_GLOBAL(gPhysics);
	DESTROY_GLOBAL(gShaderCompiler);
	DESTROY_GLOBAL(gMaterialManager);
	DESTROY_GLOBAL(gWorldGrid);
	DESTROY_GLOBAL(gThreadManager);
	DESTROY_GLOBAL(gScriptManager);
	DESTROY_GLOBAL(gWorld);
	DESTROY_GLOBAL(gProbeManager);
	DESTROY_GLOBAL(gCVars);
}

} // namespace Globals

} // namespace fx
