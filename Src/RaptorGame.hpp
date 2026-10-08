#pragma once

#include "Blockout.hpp"
#include "CommandConsole.hpp"
#include "Object/ObjectManager.hpp"

#include <Asset/AssetTicket.hpp>
#include <Asset/ConfigFile.hpp>
#include <Object/Object.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/Ragdoll.hpp>
#include <Player.hpp>
#include <Renderer/Exposure.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>
#include <atomic>
#include <memory>
#include <vector>

class ShadowDirectional;

namespace fx {

class Image;
class CVarValue;


/////////////////////////////////////
// Editor modes
/////////////////////////////////////


// class EditorModeMoveCollider : public BaseEditorMode
// {
// public:
// 	EditorModeMoveCollider() = delete;
// 	EditorModeMoveCollider(Ref<PerspectiveCamera> camera) { this->pCamera = camera; }

// 	void Update(const World& scene, const Vec3f movement_vector) override;
// 	void OnLeave(const World& scene) override;

// 	~EditorModeMoveCollider() override {};
// };


// class EditorModeScaleCollider : public BaseEditorMode
// {
// public:
// 	EditorModeScaleCollider() = delete;
// 	EditorModeScaleCollider(Ref<PerspectiveCamera> camera) { this->pCamera = camera; }

// 	void Update(const World& scene, const Vec3f movement_vector) override;
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
	void InitEngine();
	void CreateLights();

	void Tick();
	void ProcessControls();

	void ReloadWorldFile();
	void ReloadBlockout();
	void UpdateWindowTitle();
	void ReloadScripts();

	void DestroyGame();

	void ToggleEditorMode();

	void RenderText();
	void RenderCrosshair();

	struct RagdollDummy;

	void RequestRagdollDrop();
	void UpdateObjectScripts();
	void UpdateRagdollDummies();
	void DropRagdollDummy(Object* model);
	void DestroyRagdollDummy(size_t index);
	void CleanWorld();
	void AddDummyWound(RagdollDummy& dummy, const physics::RagdollHit& hit);
	void SpawnRagdollBlood(float32& cooldown, const physics::RagdollImpact& impact, float32 min_speed);

	void RequestRagdollTemplate();
	void SpawnDebugNPC(Object* model);

public:
	Ref<LightDirectional> pSun { nullptr };

	// TODO: Player attachment system
	TSRef<Object> pPistolObject { nullptr };
	TSRef<Object> pArmsObject { nullptr };
	TSRef<Object> pHelmetObject { nullptr };

	BoneId RHandBone = BoneNull;

	double DeltaTime = 1.0f / 60.0f;

	double Fps = 0.0;
	double FrameTimeMs = 0.0;

	Quat PistolRotationGoal = Quat::scIdentity;
	ObjectManager ObjectManager;

	bool bInCommandMode = false;

private:
	uint64 mLastTick = 0;

	double mFpsWindowTime = 0.0;
	uint64 mFpsWindowStartFrame = 0;

	CVarValue* mpShowFpsCVar = nullptr;

	String mBaseWindowTitle;
	String mTitleBlockoutPath;
	bool mbTitleShown = false;

	// $i_show_gpu shows the GPU time of each stage. $r_probes and $r_decals switch the probes and decals off when 0
	CVarValue* mpShowGpuCVar = nullptr;
	CVarValue* mpProbesCVar = nullptr;
	CVarValue* mpReflectionProbesCVar = nullptr;
	CVarValue* mpReflectionDebugCVar = nullptr;
	CVarValue* mpDecalsCVar = nullptr;
	CVarValue* mpDebugBoundsCVar = nullptr;
	CVarValue* mpExposureCVar = nullptr;
	CVarValue* mpApertureCVar = nullptr;
	CVarValue* mpShutterCVar = nullptr;
	CVarValue* mpIsoCVar = nullptr;
	CVarValue* mpTonemapperCVar = nullptr;

	void UpdateExposure();

	Object* mpRaycastHitMarker = nullptr;
	ObjectID mEditorSelectedObject = ObjectID::scNull;

	MaterialID mBlockoutMaterial = MaterialID::scNull;

	ConfigFile Config;

	Console mCommandConsole;

	struct RagdollDummy
	{
		ObjectID RootID = ObjectID::scNull;
		std::unique_ptr<physics::Ragdoll> pRagdoll;
		float32 BloodCooldown = 0.0f;
		ObjectID SkinnedID = ObjectID::scNull;
	};

	static constexpr size_t scMaxRagdollDummies = 8;
	static constexpr float32 scDefaultRagdollBloodSpeed = 3.5f;

	std::vector<RagdollDummy> mRagdollDummies;
	std::vector<physics::RagdollImpact> mRagdollImpacts;
	AssetTicket mRagdollTemplateTicket { nullptr };
	std::atomic<Object*> mpRagdollTemplate = nullptr;
	bool mbRagdollTemplateRequested = false;
	uint32 mRagdollDropsPending = 0;
	uint32 mRagdollSpawnCount = 0;

	static constexpr size_t scMaxDebugNPCs = 8;

	uint32 mNPCSpawnsPending = 0;
	uint32 mNPCSpawnCount = 0;
	CVarValue* mpRagdollBloodSpeedCVar = nullptr;
	CVarValue* mpNPCImpactDamageCVar = nullptr;
	CVarValue* mpNPCImpactSpeedCVar = nullptr;
	CVarValue* mpNPCReactStrengthCVar = nullptr;

	AssetTicket mCrosshairTicket { nullptr };
	std::atomic<Image*> mpCrosshair = nullptr;
};

} // namespace fx
