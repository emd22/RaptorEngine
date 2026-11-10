#include "SpawnEditor.hpp"

#ifdef FX_IS_EDITOR

#include "LightEditor.hpp"
#include "RaptorEditor.hpp"

#include <Color.hpp>
#include <Editor/Operation/WorldOperation.hpp>
#include <Engine.hpp>
#include <Physics/PhysicsPlayer.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <World.hpp>

namespace fx::editor {

float32 SpawnEditor::GetBodyHeight() { return physics::PhysicsPlayer::scStandingHeight; }

float32 SpawnEditor::GetEyeHeight() { return physics::PhysicsPlayer::scEyeHeight; }

void SpawnEditor::Move(const FVec3 offset)
{
	const World::PlayerSpawnPoint& spawn = gWorld->PlayerSpawn;

	Push(spawn.Position + offset, spawn.Direction.Normalize(), true);
}

void SpawnEditor::Aim(const FVec3 direction)
{
	if (direction.Length() < scEditTolerance) {
		return;
	}

	Push(gWorld->PlayerSpawn.Position, SnapDirection(direction).Normalize(), true);
}

void SpawnEditor::ResetToDefault()
{
	World::PlayerSpawnPoint defaults;

	Push(defaults.Position, defaults.Direction, false);
}

void SpawnEditor::Push(const FVec3 position, const FVec3 direction, bool custom)
{
	const World::PlayerSpawnPoint& spawn = gWorld->PlayerSpawn;
	const FVec3 direction_before = spawn.Direction.Normalize();

	if (position.IsCloseTo(spawn.Position, scEditTolerance) && direction.IsCloseTo(direction_before, scEditTolerance) &&
		custom == spawn.bCustom) {
		return;
	}

	gEditor->EmplaceEditOperation<SpawnTransformOperation>(
		SpawnTransformOperation::State { spawn.Position, direction_before, spawn.bCustom },
		SpawnTransformOperation::State { position, direction, custom });
}

void SpawnEditor::DrawMarker(bool selected)
{
	const World::PlayerSpawnPoint& spawn = gWorld->PlayerSpawn;
	Color color = gEditor->GetTheme().Marker.SpawnIdle;

	if (selected) {
		color = gEditor->GetTheme().Marker.Spawn;
	}

	const FVec3 half_extent(scHalfWidth, GetBodyHeight() * 0.5f, scHalfWidth);
	const FVec3 body_center = spawn.Position + FVec3(0.0f, half_extent.Y, 0.0f);

	const FVec3 eye = spawn.Position + FVec3(0.0f, GetEyeHeight(), 0.0f);
	const FVec3 tip = eye + (spawn.Direction.Normalize() * scDirectionLength);

	renderer::gDebugDraw->WireBox(body_center, half_extent, FQuat::scIdentity, color);
	renderer::gDebugDraw->Line(eye, tip, color);
	renderer::gDebugDraw->SolidBox(tip, FVec3(scDirectionTipHalfExtent), FQuat::scIdentity, color);
}

} // namespace fx::editor

#endif
