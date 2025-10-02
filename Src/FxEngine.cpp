#include "FxEngine.hpp"


RxRenderBackend* gRenderer = nullptr;
FxPhysicsJolt* gPhysics = nullptr;

void FxEngineGlobalsInit()
{
    gRenderer = new RxRenderBackend;
    gPhysics = new FxPhysicsJolt;
}

void FxEngineGlobalsDestroy()
{
    delete gRenderer;
    delete gPhysics;
}
