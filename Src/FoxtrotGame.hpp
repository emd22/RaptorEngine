#pragma once

#include "ObjectManager.hpp"

#include <Asset/ConfigFile.hpp>
#include <Object.hpp>
#include <Player.hpp>
#include <Scene.hpp>

class ShadowDirectional;

namespace fx {


enum class eEditorMode : int32
{
    MoveCollider,
    ScaleCollider,

    Default,
};

class BaseEditorMode
{
public:
    BaseEditorMode() = default;

    virtual void Update(const Scene& scene, const Vec3f& movement_vector) = 0;
    virtual void OnLeave(const Scene& scene) = 0;

    virtual ~BaseEditorMode() {};

public:
    Ref<PerspectiveCamera> pCamera { nullptr };
};

/////////////////////////////////////
// Editor modes
/////////////////////////////////////


class EditorModeMoveCollider : public BaseEditorMode
{
public:
    EditorModeMoveCollider() = delete;
    EditorModeMoveCollider(Ref<PerspectiveCamera> camera) { this->pCamera = camera; }

    void Update(const Scene& scene, const Vec3f& movement_vector) override;
    void OnLeave(const Scene& scene) override;

    ~EditorModeMoveCollider() override {};
};


class EditorModeScaleCollider : public BaseEditorMode
{
public:
    EditorModeScaleCollider() = delete;
    EditorModeScaleCollider(Ref<PerspectiveCamera> camera) { this->pCamera = camera; }

    void Update(const Scene& scene, const Vec3f& movement_vector) override;
    void OnLeave(const Scene& scene) override;

    ~EditorModeScaleCollider() override {};
};


/////////////////////////////////////
// Game class
/////////////////////////////////////

class FoxtrotGame
{
public:
    FoxtrotGame();

    void CreateGame();


    ~FoxtrotGame();

private:
    void AddEditorModes();

    void InitEngine();
    void CreateLights();

    void Tick();
    void ProcessControls();

    void LoadOffsetsFile();

    void DestroyGame();

    void NextEditorMode();
    void PrevEditorMode();
    void SwitchEditorMode(eEditorMode mode);


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

    BaseEditorMode* SelectedEditorMode = nullptr;
    eEditorMode EditorModeType = eEditorMode::Default;
    SizedArray<BaseEditorMode*> EditorModes;

    Ref<PerspectiveCamera> pEditorCamera { nullptr };

private:
    uint64 mLastTick = 0;
    Scene mMainScene {};


    ConfigFile Config;
};

} // namespace fx
