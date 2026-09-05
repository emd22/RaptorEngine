#pragma once

#include "Blockout.hpp"
#include "InGameEditor.hpp"
#include "Object/ObjectManager.hpp"

#include <Asset/ConfigFile.hpp>
#include <Object/Object.hpp>
#include <Player.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>

class ShadowDirectional;

namespace fx {


/////////////////////////////////////
// Editor modes
/////////////////////////////////////


// class EditorModeMoveCollider : public BaseEditorMode
// {
// public:
// 	EditorModeMoveCollider() = delete;
// 	EditorModeMoveCollider(Ref<PerspectiveCamera> camera) { this->pCamera = camera; }

// 	void Update(const World& scene, const Vec3f& movement_vector) override;
// 	void OnLeave(const World& scene) override;

// 	~EditorModeMoveCollider() override {};
// };


// class EditorModeScaleCollider : public BaseEditorMode
// {
// public:
// 	EditorModeScaleCollider() = delete;
// 	EditorModeScaleCollider(Ref<PerspectiveCamera> camera) { this->pCamera = camera; }

// 	void Update(const World& scene, const Vec3f& movement_vector) override;
// 	void OnLeave(const World& scene) override;

// 	~EditorModeScaleCollider() override {};
// };


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
	void SwitchEditorMode(eEditorMode mode);

	void RenderText();

	Vec3f GetCameraForwardDominantAxis() const;

public:
	Ref<LightDirectional> pSun { nullptr };

	// TODO: Player attachment system
	TSRef<Object> pPistolObject { nullptr };
	TSRef<Object> pArmsObject { nullptr };
	TSRef<Object> pHelmetObject { nullptr };

	BoneId RHandBone = BoneNull;

	double FrameTimeAvg = 0.0f;
	double DeltaTime = 1.0f / 60.0f;

	Quat PistolRotationGoal = Quat::scIdentity;
	ObjectManager ObjectManager;

	Vec3f PistolOffset = Vec3f::sZero;
	Vec3f ArmsOffset = Vec3f::sZero;

	EditorMode* pSelectedEditorMode = nullptr;
	eEditorMode EditorModeType = eEditorMode::Simulate;
	SizedArray<EditorMode*> EditorModes;


private:
	uint64 mLastTick = 0;

	ObjectID mRaycastHitMarker = ObjectID::scNull;
	ObjectID mEditorSelectedObject = ObjectID::scNull;

	MaterialID mBlockoutMaterial = MaterialID::scNull;

	ConfigFile Config;
};

} // namespace fx
