#include "FxEngine.hpp"

#include <Asset/FxShaderCompiler.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <FxMaterial.hpp>
#include <FxObjectManager.hpp>
#include <Physics/PhJolt.hpp>

PhJolt* gPhysics = nullptr;
FxShaderCompiler* gShaderCompiler = nullptr;
FxObjectManager* gObjectManager = nullptr;
FxMaterialManager* gMaterialManager = nullptr;
FxMemPool* gEnginePool = nullptr;

#define DESTROY_GLOBAL(name_)                                                                                          \
    delete name_;                                                                                                      \
    name_ = nullptr


namespace FxGlobals {

void Init()
{
    gPhysics = new PhJolt;
    gShaderCompiler = new FxShaderCompiler;
    gObjectManager = new FxObjectManager;
    gMaterialManager = new FxMaterialManager;
}


void Destroy()
{
    DESTROY_GLOBAL(gPhysics);
    DESTROY_GLOBAL(gShaderCompiler);
    DESTROY_GLOBAL(gObjectManager);
    DESTROY_GLOBAL(gMaterialManager);
}

} // namespace FxGlobals
