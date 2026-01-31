#pragma once

#include "FxObjectManager.hpp"

#include <FxObject.hpp>
#include <FxPlayer.hpp>
#include <FxScene.hpp>

class RxShadowDirectional;

class FoxtrotGame
{
public:
    FoxtrotGame();

    void CreateGame();


    ~FoxtrotGame();

private:
    void InitEngine();
    void CreateLights();

    void Tick();
    void ProcessControls();


    void DestroyGame();


public:
    FxPlayer Player {};

    FxRef<FxLightDirectional> pSun { nullptr };

    // TODO: Player attachment system
    FxRef<FxObject> pPistolObject { nullptr };
    FxRef<FxObject> pHelmetObject { nullptr };
    FxRef<FxObject> pLevelObject { nullptr };

    FxRef<FxObject> pHelmetInstance { nullptr };


    FxQuat PistolRotationGoal = FxQuat::sIdentity;

    FxRef<FxPrimitiveMesh<FxMeshGen::PositionVertex>> pSkyboxMesh { nullptr };
    double DeltaTime = 1.0f / 60.0f;

    FxObjectManager ObjectManager;

private:
    uint64 mLastTick = 0;
    FxScene mMainScene {};
};
