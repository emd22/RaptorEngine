#include "SpawnEditor.hpp"

#ifdef FX_IS_EDITOR

#include "LightEditor.hpp"
#include "RaptorEditor.hpp"

#include <Color.hpp>
#include <Controls.hpp>
#include <Engine.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Physics/PhysicsPlayer.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <World.hpp>

namespace fx::editor {

static constexpr float32 scAimFallbackDistance = 10.0f;
static constexpr float32 scAimRange = 100.0f;

static constexpr float32 scPlayerHalfWidth = 0.3f;
static constexpr float32 scEyeHeight = physics::PhysicsPlayer::scStandingHeight;
static constexpr float32 scDirectionLength = 1.5f;
static constexpr float32 scDirectionTipHalfExtent = 0.05f;

static constexpr float32 scEditTolerance = 0.0005f;
static constexpr float32 scHeightSnapStepsPerSecond = 8.0f;

static const Color scSpawnColor = Color::FromRGBA(70, 220, 255, 255);
static const Color scEditingSpawnColor = Color::FromRGBA(130, 255, 130, 255);

void SpawnEditor::Enter() { mbMoving = false; }

void SpawnEditor::Leave()
{
	CommitEdit();
	mbMoving = false;
}

void SpawnEditor::Begin()
{
	mbMoving = true;
	mMoveOrigin = gWorld->PlayerSpawn.Position;
	mPlayerOrigin = gWorld->Player.Position;
	mHeightOffset = 0.0f;

	BeginEdit();
}

void SpawnEditor::Update(float32 delta_time) { Tick(delta_time); }

void SpawnEditor::Finalize() { mbMoving = false; }

void SpawnEditor::Cancel()
{
	mbMoving = false;
	mbEditing = false;
}

void SpawnEditor::Controls() { Tick(0.0f); }

void SpawnEditor::Tick(float32 delta_time)
{
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_BACKSPACE)) {
		CommitEdit();
		ResetToDefault();
	}

	if (ControlManager::IsKeyPressed(eKey::FX_KEY_RETURN)) {
		gWorld->RespawnPlayer();
	}

	const bool aiming = ControlManager::IsKeyDown(eKey::FX_MOUSE_RIGHT);

	if (aiming) {
		BeginEdit();
		AimSpawn();
	}

	if (mbMoving) {
		MoveSpawn(delta_time);
	}

	if (!aiming && !mbMoving) {
		CommitEdit();
	}

	DrawMarker();
}

void SpawnEditor::DrawMarker()
{
	const World::PlayerSpawnPoint& spawn = gWorld->PlayerSpawn;
	const Color color = mbEditing ? scEditingSpawnColor : scSpawnColor;

	const Vec3f half_extent(scPlayerHalfWidth, scEyeHeight * 0.5f, scPlayerHalfWidth);
	const Vec3f body_center = spawn.Position + Vec3f(0.0f, half_extent.Y, 0.0f);

	const Vec3f eye = spawn.Position + Vec3f(0.0f, scEyeHeight, 0.0f);
	const Vec3f tip = eye + (spawn.Direction.Normalize() * scDirectionLength);

	renderer::gDebugDraw->WireBox(body_center, half_extent, Quat::scIdentity, color);
	renderer::gDebugDraw->Line(eye, tip, color);
	renderer::gDebugDraw->SolidBox(tip, Vec3f(scDirectionTipHalfExtent), Quat::scIdentity, color);
}

void SpawnEditor::BeginEdit()
{
	if (mbEditing) {
		return;
	}

	mbEditing = true;
	mPositionBefore = gWorld->PlayerSpawn.Position;
	mDirectionBefore = gWorld->PlayerSpawn.Direction.Normalize();
	mbCustomBefore = gWorld->PlayerSpawn.bCustom;
}

void SpawnEditor::CommitEdit()
{
	if (!mbEditing) {
		return;
	}

	mbEditing = false;

	const Vec3f position_after = gWorld->PlayerSpawn.Position;
	const Vec3f direction_after = gWorld->PlayerSpawn.Direction.Normalize();

	if (position_after.IsCloseTo(mPositionBefore, scEditTolerance) &&
		direction_after.IsCloseTo(mDirectionBefore, scEditTolerance)) {
		return;
	}

	EditOperation op {
		.Type = EditOperation::eType::SpawnTransform,
		.ValueA = EditOperationValue(mPositionBefore),
		.ValueB = EditOperationValue(position_after),
	};

	op.Spawn.DirectionBefore = mDirectionBefore;
	op.Spawn.DirectionAfter = direction_after;
	op.Spawn.bCustomBefore = mbCustomBefore;
	op.Spawn.bCustomAfter = true;

	gEditor->PushEditOperation(op);
}

void SpawnEditor::MoveSpawn(float32 delta_time)
{
	const Vec3f player_delta = gWorld->Player.Position - mPlayerOrigin;

	const float32 height_speed = delta_time * gEditor->GetSnapStep() * scHeightSnapStepsPerSecond;

	if (ControlManager::IsKeyDown(eKey::FX_KEY_E)) {
		mHeightOffset += height_speed;
	}
	if (ControlManager::IsKeyDown(eKey::FX_KEY_Q)) {
		mHeightOffset -= height_speed;
	}

	const Vec3f movement = gEditor->SnapToGrid(Vec3f(player_delta.X, mHeightOffset, player_delta.Z));

	gWorld->PlayerSpawn.Position = mMoveOrigin + movement;
}

void SpawnEditor::AimSpawn()
{
	Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	const Vec3f origin = camera->Position;
	const Vec3f forward = camera->GetForwardVector().Normalize();

	const physics::RayResult hit = gPhysics->pBackend->Raycast(origin, forward * scAimRange);
	const Vec3f target = hit.bHit ? hit.Point : (origin + (forward * scAimFallbackDistance));

	const Vec3f eye = gWorld->PlayerSpawn.Position + Vec3f(0.0f, scEyeHeight, 0.0f);
	const Vec3f to_target = target - eye;

	if (to_target.Length() < 0.05f) {
		return;
	}

	gWorld->PlayerSpawn.Direction = SnapDirection(to_target);
}

void SpawnEditor::PlaceAtCrosshair()
{
	CommitEdit();

	Ref<PerspectiveCamera>& camera = gWorld->Player.pCamera;

	const Vec3f origin = camera->Position;
	const Vec3f forward = camera->GetForwardVector().Normalize();

	const physics::RayResult hit = gPhysics->pBackend->Raycast(origin, forward * scAimRange);
	const Vec3f position = gEditor->SnapToGrid(hit.bHit ? hit.Point : (origin + (forward * scAimFallbackDistance)));

	EditOperation op {
		.Type = EditOperation::eType::SpawnTransform,
		.ValueA = EditOperationValue(gWorld->PlayerSpawn.Position),
		.ValueB = EditOperationValue(position),
	};

	op.Spawn.DirectionBefore = gWorld->PlayerSpawn.Direction.Normalize();
	op.Spawn.DirectionAfter = op.Spawn.DirectionBefore;
	op.Spawn.bCustomBefore = gWorld->PlayerSpawn.bCustom;
	op.Spawn.bCustomAfter = true;

	gEditor->PushEditOperation(op);
}

void SpawnEditor::ResetToDefault()
{
	World::PlayerSpawnPoint defaults;

	EditOperation op {
		.Type = EditOperation::eType::SpawnTransform,
		.ValueA = EditOperationValue(gWorld->PlayerSpawn.Position),
		.ValueB = EditOperationValue(defaults.Position),
	};

	op.Spawn.DirectionBefore = gWorld->PlayerSpawn.Direction.Normalize();
	op.Spawn.DirectionAfter = defaults.Direction;
	op.Spawn.bCustomBefore = gWorld->PlayerSpawn.bCustom;
	op.Spawn.bCustomAfter = false;

	gEditor->PushEditOperation(op);
}

} // namespace fx::editor

#endif
