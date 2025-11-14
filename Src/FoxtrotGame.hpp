#pragma once

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

    FxRef<FxPrimitiveMesh<FxMeshGen::PositionVertex>> pSkyboxMesh { nullptr };
    float DeltaTime = 1.0f;

private:
    uint64 mLastTick = 0;
    FxScene mMainScene {};
};
