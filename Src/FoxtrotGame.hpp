#pragma once

#include "FxObjectManager.hpp"

#include <FxObject.hpp>
#include <FxPlayer.hpp>
#include <FxScene.hpp>

class FoxtrotGame
{
public:
    FoxtrotGame();

    void CreateGame();


    ~FoxtrotGame();

private:
    void InitEngine();

    void CreateSkybox();
    void CreateLights();

    void Tick();
    void ProcessControls();


    void DestroyGame();


public:
    FxPlayer Player {};

    // TODO: Player attachment system
    FxRef<FxObject> pPistolObject { nullptr };
    FxRef<FxObject> pHelmetObject { nullptr };

    FxQuat PistolRotationGoal = FxQuat::sIdentity;

    FxRef<FxPrimitiveMesh<FxMeshGen::PositionVertex>> pSkyboxMesh { nullptr };
    double DeltaTime = 1.0f;

    FxObjectManager ObjectManager;

private:
    uint64 mLastTick = 0;
    FxScene mMainScene {};
};
