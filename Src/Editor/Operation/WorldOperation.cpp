#include "WorldOperation.hpp"

#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Script/ObjectScripts.hpp>
#include <World.hpp>


namespace fx {

namespace editor {

/////////////////////////////////////
// Player spawn
/////////////////////////////////////

SpawnTransformOperation::SpawnTransformOperation(const State& before, const State& after)
	: EditOperation(1), mBefore(before), mAfter(after)
{
}

void SpawnTransformOperation::Execute(EditorSelection& selection)
{
	gWorld->PlayerSpawn.Position = mAfter.Position;
	gWorld->PlayerSpawn.Direction = mAfter.Direction;
	gWorld->PlayerSpawn.bCustom = mAfter.bCustom;
}

void SpawnTransformOperation::Undo(EditorSelection& selection)
{
	gWorld->PlayerSpawn.Position = mBefore.Position;
	gWorld->PlayerSpawn.Direction = mBefore.Direction;
	gWorld->PlayerSpawn.bCustom = mBefore.bCustom;
}


} // namespace editor
} // namespace fx
