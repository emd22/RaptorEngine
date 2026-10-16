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

static constexpr float32 scDirectionLength = 1.5f;
static constexpr float32 scDirectionTipHalfExtent = 0.05f;

static constexpr float32 scEditTolerance = 0.0005f;


float32 SpawnEditor::GetBodyHeight() { return physics::PhysicsPlayer::scStandingHeight; }

float32 SpawnEditor::GetEyeHeight() { return physics::PhysicsPlayer::scEyeHeight; }

void SpawnEditor::Move(const Vec3f offset)
{
	const World::PlayerSpawnPoint& spawn = gWorld->PlayerSpawn;

	Push(spawn.Position + offset, spawn.Direction.Normalize(), true);
}

void SpawnEditor::Aim(const Vec3f direction)
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

void SpawnEditor::Push(const Vec3f position, const Vec3f direction, bool custom)
{
	const World::PlayerSpawnPoint& spawn = gWorld->PlayerSpawn;
	const Vec3f direction_before = spawn.Direction.Normalize();

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

	const Vec3f half_extent(scHalfWidth, GetBodyHeight() * 0.5f, scHalfWidth);
	const Vec3f body_center = spawn.Position + Vec3f(0.0f, half_extent.Y, 0.0f);

	const Vec3f eye = spawn.Position + Vec3f(0.0f, GetEyeHeight(), 0.0f);
	const Vec3f tip = eye + (spawn.Direction.Normalize() * scDirectionLength);

	renderer::gDebugDraw->WireBox(body_center, half_extent, Quat::scIdentity, color);
	renderer::gDebugDraw->Line(eye, tip, color);
	renderer::gDebugDraw->SolidBox(tip, Vec3f(scDirectionTipHalfExtent), Quat::scIdentity, color);
}

} // namespace fx::editor

#endif
