#pragma once

#include "Blockout.hpp"
#include "CommandConsole.hpp"
#include "InGameEditor.hpp"
#include "Object/ObjectManager.hpp"

#include <Asset/AssetTicket.hpp>
#include <Asset/ConfigFile.hpp>
#include <Object/Object.hpp>
#include <Player.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>
#include <atomic>

class ShadowDirectional;

namespace fx {

class Image;


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

class RaptorGame
{
public:
	RaptorGame();

	void CreateGame();


	~RaptorGame();

private:
	void AddEditorModes();

	void InitEngine();
	void CreateLights();

	void Tick();
	void ProcessControls();

	void ReloadWorldFile();
	void ReloadBlockout();
	void ReloadScripts();

	void DestroyGame();

	void ToggleEditorMode();

	void RenderText();
	void RenderCrosshair();

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

	bool bInCommandMode = false;

private:
	uint64 mLastTick = 0;

	Object* mpRaycastHitMarker = nullptr;
	ObjectID mEditorSelectedObject = ObjectID::scNull;

	MaterialID mBlockoutMaterial = MaterialID::scNull;

	ConfigFile Config;

	Console mCommandConsole;

	AssetTicket mCrosshairTicket { nullptr };
	std::atomic<Image*> mpCrosshair = nullptr;
};

} // namespace fx
