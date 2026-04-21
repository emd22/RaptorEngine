#pragma once

#include "ObjectManager.hpp"

#include <Asset/ConfigFile.hpp>
#include <Object.hpp>
#include <Player.hpp>
#include <Scene.hpp>

class ShadowDirectional;

namespace fx {

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
    Player Player {};

    Ref<LightDirectional> pSun { nullptr };

    // TODO: Player attachment system
    TSRef<Object> pPistolObject { nullptr };
    TSRef<Object> pArmsObject { nullptr };
    TSRef<Object> pHelmetObject { nullptr };

    BoneId RHandBone = BoneNull;

    double FrameTimeAvg = 0.0f;
    double DeltaTime = 1.0f / 60.0f;

    Quat PistolRotationGoal = Quat::sIdentity;
    ObjectManager ObjectManager;

    Vec3f PistolOffset = Vec3f::sZero;
    Vec3f ArmsOffset = Vec3f::sZero;

private:
    uint64 mLastTick = 0;
    Scene mMainScene {};

    ConfigFile Config;
};

} // namespace fx
