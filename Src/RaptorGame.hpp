#pragma once

#include "Blockout.hpp"
#include "CommandConsole.hpp"
#include "Object/ObjectManager.hpp"

#include <Asset/AssetTicket.hpp>
#include <Asset/ConfigFile.hpp>
#include <Controller/NoClipController.hpp>
#include <Object/Object.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Player.hpp>
#include <Ragdoll/Ragdoll.hpp>
#include <Renderer/Camera/BaseCamera.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

class ShadowDirectional;

namespace fx {

class Image;
class CVarValue;
struct NPCTuning;


/////////////////////////////////////
// Editor modes
/////////////////////////////////////


// class EditorModeMoveCollider : public BaseEditorMode
// {
// public:
// 	EditorModeMoveCollider() = delete;
// 	EditorModeMoveCollider(Ref<FPSCamera> camera) { this->pCamera = camera; }

// 	void Update(const World& scene, const Vec3f movement_vector) override;
// 	void OnLeave(const World& scene) override;

// 	~EditorModeMoveCollider() override {};
// };


// class EditorModeScaleCollider : public BaseEditorMode
// {
// public:
// 	EditorModeScaleCollider() = delete;
// 	EditorModeScaleCollider(Ref<FPSCamera> camera) { this->pCamera = camera; }

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
	void ToggleFlyCamera();
	void MoveFlyCamera();

	void RenderText();
	void RenderCrosshair();

	void RequestRagdollDrop();
	void UpdateObjectScripts();
	void UpdateRagdollDummies();
	void PlayImpactSounds();
	void DropRagdollDummy(Object* model);
	void CleanWorld();
	void ReadNPCTuning(NPCTuning& tuning) const;
	physics::eCharacterRagdollCollision GetPlayerRagdollCollision() const;
	static float32 GetCVarFloat(const CVarValue* cvar, float32 fallback);

	void RequestRagdollTemplate();
	void SpawnDebugNPC(Object* model);
	void FireCube();
	void DestroyFiredCube(Hash32 name_hash);
	void ClearFiredCubes();

public:
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
	bool mbTitleCompiled = false;
	bool mbTitleModified = false;

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

	static constexpr size_t scMaxRagdollDummies = 8;
	static constexpr float32 scDefaultRagdollBloodSpeed = 3.5f;

	static constexpr float32 scImpactSoundMinSpeed = 1.5f;
	static constexpr float32 scImpactSoundFullSpeed = 8.0f;
	static constexpr float32 scImpactSoundMinVolume = 0.15f;
	static constexpr float64 scImpactSoundCooldown = 0.12;
	static constexpr size_t scMaxImpactSoundBodies = 512;

	static constexpr float32 scClosedPortalGain = 0.2f;

	std::vector<uint8> mListenerConnectedAreas;
	int32 mListenerArea = -1;

	SizedArray<physics::SoundImpact> mSoundImpacts;
	std::unordered_map<uint32, float64> mImpactSoundTimes;
	float64 mImpactSoundClock = 0.0;

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
	CVarValue* mpNPCImpactSafeSpeedCVar = nullptr;
	CVarValue* mpNPCStrengthCVar = nullptr;
	CVarValue* mpNPCRootAssistCVar = nullptr;
	CVarValue* mpNPCBalanceCVar = nullptr;
	CVarValue* mpNPCBalanceAssistCVar = nullptr;
	CVarValue* mpNPCBalanceReleaseCVar = nullptr;
	CVarValue* mpNPCBalanceRecoveryCVar = nullptr;
	CVarValue* mpNPCMaxActiveCVar = nullptr;
	CVarValue* mpNPCActiveDistanceCVar = nullptr;
	CVarValue* mpNPCBumpSpeedCVar = nullptr;
	CVarValue* mpNPCBumpImpulseCVar = nullptr;
	CVarValue* mpNPCHitShoveCVar = nullptr;
	CVarValue* mpNPCMinShoveCVar = nullptr;
	CVarValue* mpNPCMaxShoveCVar = nullptr;
	CVarValue* mpNPCStaggerTimeCVar = nullptr;
	CVarValue* mpNPCStaggerSpeedCVar = nullptr;
	CVarValue* mpNPCStumbleTimeCVar = nullptr;
	CVarValue* mpNPCRepeatShoveCVar = nullptr;
	CVarValue* mpNPCWanderCVar = nullptr;
	CVarValue* mpPlayerRagdollCollisionCVar = nullptr;

	static constexpr uint32 scMaxFiredCubes = 32;

	/// Name hashes of the cubes fired from the camera, oldest first
	DynArray<Hash32> mFiredCubes;
	uint32 mFiredCubeCount = 0;

	Ref<FPSCamera> mpFlyCamera { nullptr };
	ViewAngles mFlyCameraAngles;
	NoClipController mFlyController;
	bool mbFlyCameraDetached = false;

	AssetTicket mCrosshairTicket { nullptr };
	std::atomic<Image*> mpCrosshair = nullptr;
};

} // namespace fx
