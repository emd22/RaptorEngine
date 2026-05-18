#include "Engine.hpp"

#include <Asset/AxManager.hpp>
#include <Asset/ShaderCompiler.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Material/MaterialManager.hpp>
#include <ObjectManager.hpp>
#include <Physics/PhJolt.hpp>

namespace fx {

PhJolt* gPhysics = nullptr;
ShaderCompiler* gShaderCompiler = nullptr;
AxManager* gAssetManager = nullptr;
ObjectManager* gObjectManager = nullptr;
MaterialManager* gMaterialManager = nullptr;
MemPool* gEnginePool = nullptr;
MemPool* gScriptMemPool = nullptr;

#define DESTROY_GLOBAL(name_)                                                                                          \
    delete name_;                                                                                                      \
    name_ = nullptr


namespace Globals {

void Init()
{
    gPhysics = new PhJolt;
    gAssetManager = new AxManager;
    gShaderCompiler = new ShaderCompiler;
    gObjectManager = new ObjectManager;
    gMaterialManager = new MaterialManager;
}


void Destroy()
{
    DESTROY_GLOBAL(gPhysics);
    DESTROY_GLOBAL(gShaderCompiler);
    DESTROY_GLOBAL(gAssetManager);
    DESTROY_GLOBAL(gObjectManager);
    DESTROY_GLOBAL(gMaterialManager);
}

} // namespace Globals

} // namespace fx
