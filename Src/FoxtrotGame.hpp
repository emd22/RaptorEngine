#pragma once

#include "FxObjectManager.hpp"

#include <Asset/FxConfigFile.hpp>
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

    void LoadOffsetsFile();

    void DestroyGame();


public:
    FxPlayer Player {};

    FxRef<FxLightDirectional> pSun { nullptr };

    // TODO: Player attachment system
    FxTSRef<FxObject> pPistolObject { nullptr };
    FxTSRef<FxObject> pArmsObject { nullptr };
    FxTSRef<FxObject> pHelmetObject { nullptr };

    FxBoneId RHandBone = FxBoneNull;

    double FrameTimeAvg = 0.0f;
    double DeltaTime = 1.0f / 60.0f;

    FxQuat PistolRotationGoal = FxQuat::sIdentity;
    FxObjectManager ObjectManager;

    FxVec3f PistolOffset = FxVec3f::sZero;
    FxVec3f ArmsOffset = FxVec3f::sZero;

private:
    uint64 mLastTick = 0;
    FxScene mMainScene {};

    FxConfigFile Config;
};
